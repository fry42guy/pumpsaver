#pragma once

/*
  Configuration export / import.

  Named JSON, not a byte dump of the NVS blob.  That blob is a raw copy of
  Settings keyed on a magic number: correct for NVS, useless as a file format.
  A field added in v0.7 would make every v0.6 backup unreadable, which is the
  opposite of what a backup is for.  Named fields import into any later
  firmware -- unknown keys are ignored, missing keys keep their current value.

  Secrets are never exported.  A config file with a customer's WiFi password in
  it ends up in an email, a support ticket, and a laptop that leaves the site.
  The network block carries SSID, broker and topic so a replacement unit is one
  paste away from configured; both passwords are then typed in once, on the
  board itself.

  There is no JSON library here on purpose.  The schema is flat -- numbers,
  short strings, and two fixed-length number arrays -- and the house already
  writes its own Modbus master rather than pull one in.
*/

// ---------------------------------------------------------------- tiny reader
// Flat objects only.  Finds "key" and reads the value that follows it.
static int cfgFind(const String &s, const char *key) {
  String pat = String("\"") + key + "\"";
  int i = s.indexOf(pat);
  if (i < 0) return -1;
  i = s.indexOf(':', i + pat.length());
  return (i < 0) ? -1 : i + 1;
}

static bool cfgHas(const String &s, const char *key) { return cfgFind(s, key) >= 0; }

static float cfgF(const String &s, const char *key, float def) {
  int i = cfgFind(s, key);
  if (i < 0) return def;
  while (i < (int)s.length() && (s[i] == ' ' || s[i] == '\t')) i++;
  if (s[i] == 't') return 1;                       // true
  if (s[i] == 'f') return 0;                       // false
  char buf[24]; int n = 0;
  while (i < (int)s.length() && n < 23 &&
         (isdigit((int)s[i]) || s[i] == '-' || s[i] == '+' || s[i] == '.' ||
          s[i] == 'e' || s[i] == 'E')) buf[n++] = s[i++];
  buf[n] = 0;
  return n ? atof(buf) : def;
}

static void cfgS(const String &s, const char *key, char *dst, size_t n) {
  int i = cfgFind(s, key);
  if (i < 0) return;
  i = s.indexOf('"', i);
  if (i < 0) return;
  int e = s.indexOf('"', i + 1);
  if (e < 0) return;
  String v = s.substring(i + 1, e);
  strlcpy(dst, v.c_str(), n);
}

// "key":[a,b,c,d]
static void cfgArr(const String &s, const char *key, float *out, int n) {
  int i = cfgFind(s, key);
  if (i < 0) return;
  i = s.indexOf('[', i);
  if (i < 0) return;
  int e = s.indexOf(']', i);
  if (e < 0) return;
  String body = s.substring(i + 1, e);
  int k = 0, start = 0;
  while (k < n) {
    int c = body.indexOf(',', start);
    String tok = (c < 0) ? body.substring(start) : body.substring(start, c);
    tok.trim();
    if (tok.length()) out[k] = tok.toFloat();
    k++;
    if (c < 0) break;
    start = c + 1;
  }
}

// ---------------------------------------------------------------- clamps
// One place, called by both /set and import, so the two can never drift.
void cfgClamp(Settings &s, PumpCfg &c) {
  s.setpoint      = limitf(5,    s.setpoint,      100);
  s.xdcrSpanPsi   = limitf(10,   s.xdcrSpanPsi,   1000);
  s.qMax60        = limitf(1,    s.qMax60,        5000);
  s.cavOnsetGPM   = limitf(1,    s.cavOnsetGPM,   5000);
  c.kp            = limitf(0,    c.kp,            10);
  c.ki            = limitf(0,    c.ki,            10);
  c.minHz         = limitf(10,   c.minHz,         55);
  c.maxHz         = limitf(20,   c.maxHz,         60);
  c.spRampPsiS    = limitf(0.1f, c.spRampPsiS,    50);
  c.spStepPsi     = limitf(1,    c.spStepPsi,     60);
  c.shutoffPsiAt60= limitf(20,   c.shutoffPsiAt60,300);
  c.capPts        = (int)limitf(2, c.capPts,      4);
  for (int i = 0; i < 4; i++) {
    c.capPsi[i]   = limitf(0, c.capPsi[i],  300);
    c.capHzPt[i]  = limitf(0, c.capHzPt[i], 60);
  }
  c.sleepDlyS     = limitf(5,    c.sleepDlyS,     600);
  c.sleepHz       = limitf(20,   c.sleepHz,       60);
  c.sleepHz2      = limitf(0,    c.sleepHz2,      10);
  c.sleepHzMargin = limitf(0.05f,c.sleepHzMargin, 15);
  c.sleepBand     = limitf(0.2f, c.sleepBand,     10);
  c.sleepBoost    = limitf(0,    c.sleepBoost,    30);
  c.boostMaxS     = limitf(5,    c.boostMaxS,     600);
  c.chargeRampS   = limitf(0,    c.chargeRampS,   120);
  c.wakeDrop      = limitf(0.5f, c.wakeDrop,      30);
  c.sleepMinS     = limitf(0,    c.sleepMinS,     600);
  c.idleGPM       = limitf(0,    c.idleGPM,       100);
  c.wakeGPM       = limitf(0,    c.wakeGPM,       100);
  c.stageUpPsi    = limitf(0.5f, c.stageUpPsi,    30);
  c.stageUpDlyS   = limitf(1,    c.stageUpDlyS,   600);
  c.stageDownHz   = limitf(20,   c.stageDownHz,   60);
  c.stageDownDlyS = limitf(1,    c.stageDownDlyS, 600);
  c.lagMinRunS    = limitf(0,    c.lagMinRunS,    3600);
}

// ---------------------------------------------------------------- export
String cfgExportJSON() {
  const PumpCfg &c = pc.cfg;
  NetCfg n = netCurrent();
  String j;
  j.reserve(1600);

  j += "{\n  \"_\": \"PumpSaver configuration -- passwords are deliberately not included\",\n";
  j += "  \"schema\": 1,\n";
  j += "  \"fw\": \"" FW_VERSION "\",\n";
  j += "  \"device\": \"" + String(gDevId) + "\",\n";
  j += "  \"upSec\": " + String(millis() / 1000) + ",\n";

  j += "\n  \"setpoint\": "       + String(S.setpoint, 2);
  j += ",\n  \"xdcrSpanPsi\": "   + String(S.xdcrSpanPsi, 1);
  j += ",\n  \"useFlow\": "       + String(S.useFlow ? "true" : "false");
  j += ",\n  \"qMax60\": "        + String(S.qMax60, 1);
  j += ",\n  \"cavOnsetGPM\": "   + String(S.cavOnsetGPM, 1);

  j += ",\n\n  \"kp\": "          + String(c.kp, 3);
  j += ",\n  \"ki\": "            + String(c.ki, 3);
  j += ",\n  \"minHz\": "         + String(c.minHz, 2);
  j += ",\n  \"maxHz\": "         + String(c.maxHz, 2);
  j += ",\n  \"spRampPsiS\": "    + String(c.spRampPsiS, 2);
  j += ",\n  \"spStepPsi\": "     + String(c.spStepPsi, 2);
  j += ",\n  \"shutoffPsiAt60\": "+ String(c.shutoffPsiAt60, 2);

  j += ",\n\n  \"capPts\": "      + String(c.capPts);
  j += ",\n  \"capEnable\": "     + String(c.capEnable ? "true" : "false");
  j += ",\n  \"capPsi\": [";
  for (int i = 0; i < 4; i++) { if (i) j += ", "; j += String(c.capPsi[i], 2); }
  j += "],\n  \"capHz\": [";
  for (int i = 0; i < 4; i++) { if (i) j += ", "; j += String(c.capHzPt[i], 2); }
  j += "]";

  j += ",\n\n  \"sleepRelShutoff\": " + String(c.sleepRelShutoff ? "true" : "false");
  j += ",\n  \"sleepHzMargin\": " + String(c.sleepHzMargin, 2);
  j += ",\n  \"sleepHz\": "       + String(c.sleepHz, 2);
  j += ",\n  \"sleepHz2\": "      + String(c.sleepHz2, 2);
  j += ",\n  \"sleepDlyS\": "     + String(c.sleepDlyS, 1);
  j += ",\n  \"chargeRampS\": "   + String(c.chargeRampS, 2);
  j += ",\n  \"sleepBand\": "     + String(c.sleepBand, 2);
  j += ",\n  \"sleepBoost\": "    + String(c.sleepBoost, 2);
  j += ",\n  \"spBoostMax\": "    + String(c.spBoostMax, 1);
  j += ",\n  \"boostMaxS\": "     + String(c.boostMaxS, 1);
  j += ",\n  \"wakeDrop\": "      + String(c.wakeDrop, 2);
  j += ",\n  \"sleepMinS\": "     + String(c.sleepMinS, 1);
  j += ",\n  \"idleGPM\": "       + String(c.idleGPM, 1);
  j += ",\n  \"wakeGPM\": "       + String(c.wakeGPM, 1);

  j += ",\n\n  \"stageUpPsi\": "  + String(c.stageUpPsi, 2);
  j += ",\n  \"stageUpDlyS\": "   + String(c.stageUpDlyS, 1);
  j += ",\n  \"stageDownHz\": "   + String(c.stageDownHz, 2);
  j += ",\n  \"stageDownDlyS\": " + String(c.stageDownDlyS, 1);
  j += ",\n  \"lagMinRunS\": "    + String(c.lagMinRunS, 1);

  // network, without secrets
  j += ",\n\n  \"net_ssid\": \""  + String(n.ssid) + "\"";
  j += ",\n  \"net_mqttOn\": "    + String(n.mqttOn ? "true" : "false");
  j += ",\n  \"net_host\": \""    + String(n.host) + "\"";
  j += ",\n  \"net_port\": "      + String(n.port);
  j += ",\n  \"net_user\": \""    + String(n.user) + "\"";
  j += ",\n  \"net_topic\": \""   + String(n.topic) + "\"";
  j += ",\n  \"net_pubMs\": "     + String(n.pubMs);
  j += "\n}\n";
  return j;
}

// ---------------------------------------------------------------- import
// Applies live, exactly as /set does.  A setpoint jump goes through the normal
// fill ramp; nothing here can start a pump, because gEnable is not in the file.
String cfgImportJSON(const String &b, bool withNet) {
  if (b.indexOf("\"schema\"") < 0)
    return "Not a PumpSaver config -- no \"schema\" key found. Nothing changed.";

  int schema = (int)cfgF(b, "schema", 0);
  if (schema < 1 || schema > 1)
    return "Config schema " + String(schema) + " is not supported by this firmware. Nothing changed.";

  Settings s = S;
  PumpCfg  c = pc.cfg;
  int applied = 0;

  #define GETF(k, dst) if (cfgHas(b, k)) { dst = cfgF(b, k, dst); applied++; }
  GETF("setpoint",       s.setpoint)
  GETF("xdcrSpanPsi",    s.xdcrSpanPsi)
  GETF("qMax60",         s.qMax60)
  GETF("cavOnsetGPM",    s.cavOnsetGPM)
  GETF("kp",             c.kp)
  GETF("ki",             c.ki)
  GETF("minHz",          c.minHz)
  GETF("maxHz",          c.maxHz)
  GETF("spRampPsiS",     c.spRampPsiS)
  GETF("spStepPsi",      c.spStepPsi)
  GETF("shutoffPsiAt60", c.shutoffPsiAt60)
  GETF("sleepHzMargin",  c.sleepHzMargin)
  GETF("sleepHz",        c.sleepHz)
  GETF("sleepHz2",       c.sleepHz2)
  GETF("sleepDlyS",      c.sleepDlyS)
  GETF("chargeRampS",    c.chargeRampS)
  GETF("sleepBand",      c.sleepBand)
  GETF("sleepBoost",     c.sleepBoost)
  GETF("spBoostMax",     c.spBoostMax)
  GETF("boostMaxS",      c.boostMaxS)
  GETF("wakeDrop",       c.wakeDrop)
  GETF("sleepMinS",      c.sleepMinS)
  GETF("idleGPM",        c.idleGPM)
  GETF("wakeGPM",        c.wakeGPM)
  GETF("stageUpPsi",     c.stageUpPsi)
  GETF("stageUpDlyS",    c.stageUpDlyS)
  GETF("stageDownHz",    c.stageDownHz)
  GETF("stageDownDlyS",  c.stageDownDlyS)
  GETF("lagMinRunS",     c.lagMinRunS)
  #undef GETF

  if (cfgHas(b, "capPts"))          { c.capPts = (int)cfgF(b, "capPts", c.capPts); applied++; }
  if (cfgHas(b, "capEnable"))       { c.capEnable = cfgF(b, "capEnable", 1) > 0.5f; applied++; }
  if (cfgHas(b, "useFlow"))         { s.useFlow = cfgF(b, "useFlow", 0) > 0.5f; applied++; }
  if (cfgHas(b, "sleepRelShutoff")) { c.sleepRelShutoff = cfgF(b, "sleepRelShutoff", 1) > 0.5f; applied++; }
  if (cfgHas(b, "capPsi"))          { cfgArr(b, "capPsi", c.capPsi,  4); applied++; }
  if (cfgHas(b, "capHz"))           { cfgArr(b, "capHz",  c.capHzPt, 4); applied++; }

  if (!applied) return "No recognised settings in that file. Nothing changed.";

  cfgClamp(s, c);
  S = s;
  pc.cfg = c;
  plant.qMax60       = S.qMax60;
  plant.shutoffPsi60 = c.shutoffPsiAt60;
  saveSettings();

  String msg = "Imported " + String(applied) + " settings";
  String from = "";
  {
    char fw[16] = ""; cfgS(b, "fw", fw, sizeof(fw));
    char dev[16] = ""; cfgS(b, "device", dev, sizeof(dev));
    if (fw[0])  from += " from fw " + String(fw);
    if (dev[0]) from += " (unit " + String(dev) + ")";
  }
  msg += from + ".";

  // Network, only when asked.  Passwords are not in the file by design, so a
  // pasted config points the unit at the right SSID and broker and then waits
  // for someone to type the two secrets on this board.
  if (withNet && cfgHas(b, "net_ssid")) {
    NetCfg n = netCurrent();
    cfgS(b, "net_ssid",  n.ssid,  sizeof(n.ssid));
    cfgS(b, "net_host",  n.host,  sizeof(n.host));
    cfgS(b, "net_user",  n.user,  sizeof(n.user));
    cfgS(b, "net_topic", n.topic, sizeof(n.topic));
    n.port   = (uint16_t)limitf(1,   cfgF(b, "net_port",  n.port),  65535);
    n.pubMs  = (uint16_t)limitf(500, cfgF(b, "net_pubMs", n.pubMs), 60000);
    n.mqttOn = cfgF(b, "net_mqttOn", n.mqttOn ? 1 : 0) > 0.5f;
    netStage(n);
    msg += " Network applied -- WiFi and broker passwords still need entering.";
  }

  if (!pc.capTableOK)
    msg += " WARNING: cap table is not monotonic (or row 1 <= Min Hz); cap parked at row 1.";

  return msg;
}
