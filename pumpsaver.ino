/*
  PumpSaver -- offline pump skid controller (Mode 1: standalone)

  Board : Waveshare ESP32-S3-RS485-CAN  ("ESP32S3 Dev Module", esp32 core 3.x)
  Drive : Invertek Optidrive E3, Modbus RTU via front RJ45 (pins 7/8), P-12 = 3
  Pins  : RS485 TX 17, RX 18, DE 21

  All control parameters live here and are set from the web UI.  There is no
  PLC in this build -- that is the CAN gateway version, and it is not this.

  Libraries: ESPAsyncWebServer + AsyncTCP (ESP32Async).  The Modbus master is
  local (see class Rtu) rather than ModbusMaster, because ModbusMaster's
  response timeout is a private compile-time constant of 2000 ms.  With a
  drive absent that stalls the control loop for seconds at a time, and drive
  discovery needs to probe addresses that are deliberately empty.

  Registers are Invertek 1-based; REG(n) = n-1 for the wire.
      1,2   control word, speed reference x10
      6,7,8 status, Hz x10, A x10
      20    AI1, 0-1000 = 0-100 %
*/

#include <WiFi.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <ESPAsyncWebServer.h>
#include "pump_control.h"
#include "plant_sim.h"

// net.h needs the version string at runtime; the macro is not visible to it.
const char *FW_VERSION_STR = FW_VERSION;
#include "net.h"

// ---------------------------------------------------------------- version
// Bump FW_VERSION on every change pass before flashing.  The number is shown
// on the serial banner, in the page header and in /status, so a board in the
// field can always be matched to a commit.  See VERSION.md for the log.
#define FW_VERSION "0.6.0"

// ---------------------------------------------------------------- build mode
// 1 = simulated plant, no drive or RS485 needed.  0 = real drives over Modbus.
// The web page shows an unmissable banner while this is 1.
#define SIM 1

#define AP_SSID        "FCW-PUMP"
#define AP_PASS        "fullcircle"
#define RS485_RX       18
#define RS485_TX       17
#define RS485_DE       21
#define DRIVE_BAUD     115200
#define TICK_MS        100          // control + I/O tick; PI sub-ticks at 50 ms
#define STATUS_EVERY   5            // poll drive status every Nth tick
#define MAX_DRIVES     2            // control block is lead + lag only
#define FIRST_ADDR     2            // address 1 is the uncommissioned slot
#define LAST_SCAN_ADDR 5
#define REG(n)         ((n) - 1)

// ---------------------------------------------------------------- modbus rtu
class Rtu {
public:
  enum Err : uint8_t { OK = 0, E_TIMEOUT, E_CRC, E_EXCEPTION, E_BADFRAME };
  uint16_t timeoutMs = 150;         // a healthy E3 answers in single-digit ms
  Err last = OK;

  void begin(HardwareSerial *s, int dePin) { ser = s; de = dePin; }

  bool readHolding(uint8_t addr, uint16_t reg, uint8_t n, uint16_t *out) {
    uint8_t req[8] = { addr, 0x03, (uint8_t)(reg >> 8), (uint8_t)reg, 0x00, n };
    uint8_t rsp[64];
    if (!txrx(req, 6, rsp, 5 + 2 * n, 0x03)) return false;
    if (rsp[2] != 2 * n) { last = E_BADFRAME; return false; }
    for (int i = 0; i < n; i++) out[i] = ((uint16_t)rsp[3 + 2 * i] << 8) | rsp[4 + 2 * i];
    return true;
  }

  bool writeSingle(uint8_t addr, uint16_t reg, uint16_t val) {
    uint8_t req[8] = { addr, 0x06, (uint8_t)(reg >> 8), (uint8_t)reg,
                       (uint8_t)(val >> 8), (uint8_t)val };
    uint8_t rsp[16];
    return txrx(req, 6, rsp, 8, 0x06);
  }

  bool writeMultiple(uint8_t addr, uint16_t reg, const uint16_t *v, uint8_t n) {
    uint8_t req[24]; int i = 0;
    req[i++] = addr; req[i++] = 0x10;
    req[i++] = reg >> 8; req[i++] = reg;
    req[i++] = 0; req[i++] = n;
    req[i++] = 2 * n;
    for (int k = 0; k < n; k++) { req[i++] = v[k] >> 8; req[i++] = v[k]; }
    uint8_t rsp[16];
    return txrx(req, i, rsp, 8, 0x10);
  }

  const char *errName() const {
    switch (last) {
      case OK:          return "ok";
      case E_TIMEOUT:   return "timeout";
      case E_CRC:       return "CRC";        // something IS there, bus unhealthy
      case E_EXCEPTION: return "exception";
      default:          return "bad frame";
    }
  }

private:
  HardwareSerial *ser = nullptr; int de = -1;

  static uint16_t crc16(const uint8_t *d, int len) {
    uint16_t c = 0xFFFF;
    for (int i = 0; i < len; i++) {
      c ^= d[i];
      for (int b = 0; b < 8; b++) c = (c & 1) ? ((c >> 1) ^ 0xA001) : (c >> 1);
    }
    return c;
  }

  bool txrx(uint8_t *req, int len, uint8_t *rsp, int want, uint8_t fn) {
    uint16_t c = crc16(req, len);
    req[len++] = c & 0xFF; req[len++] = c >> 8;

    while (ser->available()) ser->read();
    digitalWrite(de, HIGH);
    ser->write(req, len);
    ser->flush();                       // blocks until the last bit is out
    digitalWrite(de, LOW);
    while (ser->available()) ser->read();   // discard any local echo

    int got = 0;
    uint32_t deadline = millis() + timeoutMs;
    while (got < want && (int32_t)(millis() - deadline) < 0) {
      while (ser->available() && got < want) rsp[got++] = ser->read();
      if (got >= 2 && (rsp[1] & 0x80)) want = 5;   // exception frames are short
    }
    if (got < want)    { last = E_TIMEOUT; return false; }
    uint16_t rc = crc16(rsp, got - 2);
    if ((rc & 0xFF) != rsp[got - 2] || (rc >> 8) != rsp[got - 1]) { last = E_CRC; return false; }
    if (rsp[1] & 0x80) { last = E_EXCEPTION; return false; }
    if (rsp[1] != fn)  { last = E_BADFRAME; return false; }
    last = OK; return true;
  }
};

Rtu rtu;

// ---------------------------------------------------------------- drives
struct Drive {
  uint8_t  addr    = 0;
  bool     present = false;      // found by discovery, remembered afterwards
  bool     commsOK = false;
  int      fails   = 0;
  uint16_t status  = 0;
  float    hz = 0, amps = 0;
  bool     running = false, tripped = false, ready = false;
  uint8_t  tripCode = 0;
};
Drive drv[MAX_DRIVES];
bool  addr1Occupied = false;     // an uncommissioned drive is sitting at 1

// ---------------------------------------------------------------- settings
struct Settings {
  uint32_t magic = 0x50535635;       // bump when the layout changes
  PumpCfg  c;
  float    setpoint      = 55.0f;
  float    xdcrSpanPsi   = 200.0f;   // 4-20 mA full scale on the drive's AI1
  bool     useFlow       = false;    // true only when a meter is fitted

  // Pump characteristics.  Not used by the control block -- these exist so the
  // envelope plot can draw the shutoff curve and the true cavitation flow
  // contour that the piecewise cap table is an approximation of.
  float    qMax60        = 142.0f;   // gpm at zero head, 60 Hz
  float    cavOnsetGPM   = 103.0f;   // flow where the bench heard it rattle

  // Bench driving mode.  0 = set a draw and let the plant find its own
  // pressure.  1 = hold the header at a pressure you choose and let the loop
  // react to it -- an infinitely stiff source, which is the quick way to sweep
  // the cap curve and flip the sleep gates without waiting on tank dynamics.
  int      simMode       = 0;
  float    simPsi        = 55.0f;
  float    simDemandGPM  = 0.0f;
  float    simTimeScale  = 1.0f;
  float    simCapGalPsi  = 1.5f;
};
Settings S;
Preferences prefs;

// ---------------------------------------------------------------- live state
PumpControl pc;
PumpIn      pin_;
PumpOut     pout;
PlantSim    plant;

bool     gEnable  = false;       // NOT persisted -- a power cut must not restart the pump
bool     gLockout = false;       // local inhibit, survives nothing either
uint32_t gCmdReset = 0;
uint32_t lastTick = 0, tickN = 0;
float    gPsiRaw = 0;
bool     gPsiValid = false;
String   lastLog;

AsyncWebServer server(80);
DNSServer dns;

// ---------------------------------------------------------------- nvs
void loadSettings() {
  Settings d;
  prefs.begin("pumpsaver", true);
  if (prefs.isKey("blob")) prefs.getBytes("blob", &S, sizeof(S));
  prefs.end();
  if (S.magic != d.magic) { S = d; lastLog = "Settings blank or old format -- defaults loaded."; }
  pc.cfg = S.c;
  plant.capacityGalPsi = S.simCapGalPsi;
  plant.qMax60         = S.qMax60;          // sim and plot share one number
  plant.shutoffPsi60   = S.c.shutoffPsiAt60;
}
void saveSettings() {
  S.c = pc.cfg;
  prefs.begin("pumpsaver", false);
  prefs.putBytes("blob", &S, sizeof(S));
  prefs.end();
}

// ---------------------------------------------------------------- drive i/o
#if !SIM
bool probe(uint8_t addr) {
  uint16_t v[3];
  for (int try_ = 0; try_ < 3; try_++)          // one CRC hit must not hide a drive
    if (rtu.readHolding(addr, REG(6), 3, v)) return true;
  return false;
}

// Scan a RANGE and record which addresses answered.  Stopping at the first
// silence would report zero drives when drive 2 is simply powered down, and
// would silently drop to single-pump operation if drive 3 died in service.
String discover() {
  String out;
  addr1Occupied = probe(1);
  if (addr1Occupied) out += "WARNING: uncommissioned drive answering at address 1\n";

  int found = 0;
  for (uint8_t a = FIRST_ADDR; a <= LAST_SCAN_ADDR; a++) {
    bool ok = probe(a);
    if (ok && found < MAX_DRIVES) {
      drv[found].addr = a; drv[found].present = true; drv[found].fails = 0;
      out += "drive " + String(found + 1) + " at address " + String(a) + "\n";
      found++;
    } else if (ok) {
      out += "address " + String(a) + " answered but only " + String(MAX_DRIVES)
           + " drives are supported\n";
    } else if (rtu.last == Rtu::E_CRC) {
      // something is on the wire but the frame is corrupt: check A/B, baud,
      // and that exactly two ends of the segment are terminated
      out += "address " + String(a) + ": CRC errors -- bus wiring suspect\n";
    }
  }
  for (int i = found; i < MAX_DRIVES; i++) drv[i].present = false;
  if (!found) out += "no drives found (they boot slower than this board -- rescan)\n";
  return out;
}

void pollDrive(Drive &d) {
  if (!d.present) return;
  uint16_t v[3];
  if (rtu.readHolding(d.addr, REG(6), 3, v)) {
    d.fails = 0; d.commsOK = true;
    d.status  = v[0];
    d.hz      = v[1] / 10.0f;
    d.amps    = v[2] / 10.0f;
    d.running = d.status & 0x01;
    d.tripped = d.status & 0x02;
    d.ready   = d.status & 0x40;
    d.tripCode = d.status >> 8;
  } else if (++d.fails >= 3) {
    d.commsOK = false;
  }
}

void commandDrive(Drive &d, bool run, float hz, bool reset) {
  if (!d.present) return;
  uint16_t w[2];
  w[0] = (run ? 1 : 0) | (reset ? 4 : 0);
  w[1] = (uint16_t)(limitf(0, hz, 60.0f) * 10.0f + 0.5f);
  rtu.writeMultiple(d.addr, REG(1), w, 2);
}
#endif

// ---------------------------------------------------------------- web page
#include "page.h"

// ---------------------------------------------------------------- json
String statusJSON() {
  String j = "{";
  j += "\"ver\":\"" FW_VERSION "\"";
  j += ",\"sim\":" + String(SIM ? "true" : "false");
  j += ",\"psi\":" + String(pin_.psi, 1) + ",\"psiValid\":" + String(gPsiValid ? "true" : "false");
  j += ",\"spActive\":" + String(pout.spActive, 1) + ",\"hzCmd\":" + String(pout.hzCmd, 1);
  j += ",\"capHz\":" + String(pout.capHz, 1) + ",\"shutoffHz\":" + String(pout.shutoffHz, 1);
  j += ",\"state\":" + String(pout.state) + ",\"enable\":" + String(gEnable ? "true" : "false");
  j += ",\"sleepCycles\":" + String(pc.sleepCycles);
  j += ",\"addr1\":" + String(addr1Occupied ? "true" : "false");
  j += ",\"flow\":" + String(plant.flowGPM, 1) + ",\"hzAct\":" + String(plant.hzAct, 1);
  j += ",\"drives\":[";
  for (int i = 0; i < MAX_DRIVES; i++) {
    if (i) j += ",";
    j += "{\"n\":" + String(i + 1) + ",\"addr\":" + String(drv[i].addr);
    j += ",\"present\":" + String(drv[i].present ? "true" : "false");
    j += ",\"commsOK\":" + String(drv[i].commsOK ? "true" : "false");
    j += ",\"hz\":" + String(drv[i].hz, 1) + ",\"amps\":" + String(drv[i].amps, 1);
    j += ",\"running\":" + String(drv[i].running ? "true" : "false");
    j += ",\"tripped\":" + String(drv[i].tripped ? "true" : "false");
    j += ",\"tripCode\":" + String(drv[i].tripCode) + "}";
  }
  j += "]}";
  return j;
}

// Delivery at a given speed and header pressure, from the same affinity-law
// curve the envelope plot draws.  Used to say what a speed threshold means in
// gpm, because "50 Hz" tells an operator nothing.
float flowAt(float hz, float psi) {
  float r = hz / 60.0f, h0 = pc.cfg.shutoffPsiAt60 * r * r;
  if (hz < 1.0f || psi >= h0) return 0.0f;
  return S.qMax60 * r * sqrtf(1.0f - psi / h0);
}

String diagJSON() {
  const PumpDiag &d = pc.d;
  PumpCfg &c = pc.cfg;
  String j = "{";
  j += "\"sp\":" + String(d.sp, 1) + ",\"spActive\":" + String(d.spActive, 1);
  j += ",\"spEff\":" + String(d.spEff, 2) + ",\"psi\":" + String(d.psiUse, 2);
  j += ",\"err\":" + String(d.err, 2) + ",\"p\":" + String(d.pTerm, 2);
  j += ",\"i\":" + String(d.iTerm, 2) + ",\"raw\":" + String(d.raw, 2);
  j += ",\"clamp\":" + String(d.clamp, 2) + ",\"hz\":" + String(pout.hzCmd, 2);
  j += ",\"floor\":" + String(d.floorEff, 1) + ",\"cap\":" + String(pout.capHz, 2);
  j += ",\"capTgt\":" + String(d.capTarget, 2) + ",\"psiFilt\":" + String(d.psiFilt, 2);
  j += ",\"iAtFloor\":" + String(d.iAtFloor ? "true" : "false");
  j += ",\"iAtCap\":" + String(d.iAtCap ? "true" : "false");
  j += ",\"capOK\":" + String(pc.capTableOK ? "true" : "false");
  j += ",\"shSP\":" + String(d.shutoffSP, 2) + ",\"shAct\":" + String(d.shutoffActive, 2);
  j += ",\"thr1\":" + String(d.thr1, 2) + ",\"thr2\":" + String(d.thr2, 2);
  j += ",\"thr1gpm\":" + String(flowAt(d.thr1, d.sp), 1);
  j += ",\"thr2gpm\":" + String(flowAt(d.thr2, d.spActive), 1);
  j += ",\"nowgpm\":" + String(flowAt(pout.hzCmd, d.psiUse), 1);
  j += ",\"stage\":" + String(pc.sleepStage);
  j += ",\"g1spEff\":" + String(d.g1spEff ? "true" : "false");
  j += ",\"g1psi\":" + String(d.g1psi ? "true" : "false");
  j += ",\"g1hz\":" + String(d.g1hz ? "true" : "false");
  j += ",\"g2psi\":" + String(d.g2psi ? "true" : "false");
  j += ",\"g2hz\":" + String(d.g2hz ? "true" : "false");
  j += ",\"flowIdle\":" + String(d.flowIdle ? "true" : "false");
  j += ",\"useFlow\":" + String(S.useFlow ? "true" : "false");
  j += ",\"flowGPM\":" + String(pin_.flowGPM, 1);
  j += ",\"wake\":" + String(d.wake ? "true" : "false");
  j += ",\"tP1\":" + String(d.tP1, 1) + ",\"tP2\":" + String(d.tP2, 1);
  j += ",\"tMin\":" + String(d.tMin, 1) + ",\"tBst\":" + String(d.tBst, 1);
  j += ",\"dly\":" + String(c.sleepDlyS, 0) + ",\"minS\":" + String(c.sleepMinS, 0);
  j += ",\"bstS\":" + String(c.boostMaxS, 0);
  j += ",\"tUp\":" + String(d.tUp, 1) + ",\"tDn\":" + String(d.tDn, 1);
  j += ",\"tLag\":" + String(d.tLag, 1);
  j += ",\"upS\":" + String(c.stageUpDlyS, 0) + ",\"dnS\":" + String(c.stageDownDlyS, 0);
  j += ",\"lagS\":" + String(c.lagMinRunS, 0);
  j += ",\"stageUp\":" + String(d.stageUp ? "true" : "false");
  j += ",\"stageDown\":" + String(d.stageDown ? "true" : "false");
  j += ",\"lagOn\":" + String(d.lagOn ? "true" : "false");
  j += ",\"kp\":" + String(c.kp, 2) + ",\"ki\":" + String(c.ki, 2);
  j += ",\"abandons\":" + String(pc.boostAbandon) + "}";
  return j;
}

String settingsJSON() {
  PumpCfg &c = pc.cfg;
  String j = "{";
  j += "\"setpoint\":" + String(S.setpoint, 1);
  j += ",\"kp\":" + String(c.kp, 2) + ",\"ki\":" + String(c.ki, 2);
  j += ",\"minHz\":" + String(c.minHz, 1) + ",\"maxHz\":" + String(c.maxHz, 1);
  j += ",\"spRampPsiS\":" + String(c.spRampPsiS, 1) + ",\"spStepPsi\":" + String(c.spStepPsi, 1);
  j += ",\"shutoffPsiAt60\":" + String(c.shutoffPsiAt60, 1);
  j += ",\"xdcrSpanPsi\":" + String(S.xdcrSpanPsi, 0);
  j += ",\"capPts\":" + String(c.capPts) + ",\"capPsi\":[";
  for (int i = 0; i < 4; i++) j += String(c.capPsi[i], 0) + (i < 3 ? "," : "");
  j += "],\"capHz\":[";
  for (int i = 0; i < 4; i++) j += String(c.capHzPt[i], 1) + (i < 3 ? "," : "");
  j += "],\"sleepDlyS\":" + String(c.sleepDlyS, 0) + ",\"sleepHz\":" + String(c.sleepHz, 1);
  j += ",\"sleepHz2\":" + String(c.sleepHz2, 1) + ",\"sleepBand\":" + String(c.sleepBand, 1);
  j += ",\"sleepBoost\":" + String(c.sleepBoost, 1) + ",\"boostMaxS\":" + String(c.boostMaxS, 0);
  j += ",\"wakeDrop\":" + String(c.wakeDrop, 1) + ",\"sleepMinS\":" + String(c.sleepMinS, 0);
  j += ",\"idleGPM\":" + String(c.idleGPM, 1) + ",\"wakeGPM\":" + String(c.wakeGPM, 1);
  j += ",\"stageUpPsi\":" + String(c.stageUpPsi, 1) + ",\"stageUpDlyS\":" + String(c.stageUpDlyS, 0);
  j += ",\"stageDownHz\":" + String(c.stageDownHz, 1) + ",\"stageDownDlyS\":" + String(c.stageDownDlyS, 0);
  j += ",\"lagMinRunS\":" + String(c.lagMinRunS, 0);
  j += ",\"chargeRampS\":" + String(c.chargeRampS, 1);
  j += ",\"sleepHzMargin\":" + String(c.sleepHzMargin, 2);
  j += ",\"sleepRelShutoff\":" + String(c.sleepRelShutoff ? 1 : 0);
  j += ",\"useFlow\":" + String(S.useFlow ? 1 : 0);
  j += ",\"qMax60\":" + String(S.qMax60, 0) + ",\"cavOnsetGPM\":" + String(S.cavOnsetGPM, 0);
  j += ",\"capTableOK\":" + String(pc.capTableOK ? "true" : "false");
  j += ",\"simMode\":" + String(S.simMode) + ",\"simPsi\":" + String(S.simPsi, 1);
  j += ",\"simDemandGPM\":" + String(S.simDemandGPM, 1);
  j += ",\"simTimeScale\":" + String(S.simTimeScale, 0);
  j += ",\"simCapGalPsi\":" + String(S.simCapGalPsi, 2) + "}";
  return j;
}

void argS(AsyncWebServerRequest *r, const char *k, char *dst, size_t n) {
  if (!r->hasParam(k, true) && !r->hasParam(k)) return;
  String v = r->hasParam(k, true) ? r->getParam(k, true)->value() : r->getParam(k)->value();
  strlcpy(dst, v.c_str(), n);
}

float argF(AsyncWebServerRequest *r, const char *k, float d) {
  return r->hasParam(k, true) ? r->getParam(k, true)->value().toFloat() : d;
}

void setupWeb() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *r) { r->send(200, "text/html", PAGE); });
  server.on("/status", HTTP_GET, [](AsyncWebServerRequest *r) { r->send(200, "application/json", statusJSON()); });
  server.on("/settings", HTTP_GET, [](AsyncWebServerRequest *r) { r->send(200, "application/json", settingsJSON()); });
  server.on("/diag", HTTP_GET, [](AsyncWebServerRequest *r) { r->send(200, "application/json", diagJSON()); });
  server.on("/log", HTTP_GET, [](AsyncWebServerRequest *r) { r->send(200, "text/plain", lastLog); });

  server.on("/set", HTTP_POST, [](AsyncWebServerRequest *r) {
    PumpCfg c = pc.cfg;
    S.setpoint     = limitf(5, argF(r, "setpoint", S.setpoint), 100);
    S.xdcrSpanPsi  = limitf(10, argF(r, "xdcrSpanPsi", S.xdcrSpanPsi), 1000);
    c.kp           = limitf(0, argF(r, "kp", c.kp), 10);
    c.ki           = limitf(0, argF(r, "ki", c.ki), 10);
    c.minHz        = limitf(10, argF(r, "minHz", c.minHz), 55);
    c.maxHz        = limitf(20, argF(r, "maxHz", c.maxHz), 60);
    c.spRampPsiS   = limitf(0.1, argF(r, "spRampPsiS", c.spRampPsiS), 50);
    c.spStepPsi    = limitf(1, argF(r, "spStepPsi", c.spStepPsi), 60);
    c.shutoffPsiAt60 = limitf(20, argF(r, "shutoffPsiAt60", c.shutoffPsiAt60), 300);
    c.capPts       = (int)limitf(2, argF(r, "capPts", c.capPts), 4);
    for (int i = 0; i < 4; i++) {
      c.capPsi[i]  = limitf(0, argF(r, (String("cp") + i).c_str(), c.capPsi[i]), 300);
      c.capHzPt[i] = limitf(0, argF(r, (String("ch") + i).c_str(), c.capHzPt[i]), 60);
    }
    c.sleepDlyS    = limitf(5, argF(r, "sleepDlyS", c.sleepDlyS), 600);
    c.sleepHz      = limitf(20, argF(r, "sleepHz", c.sleepHz), 60);
    c.sleepHz2     = limitf(0, argF(r, "sleepHz2", c.sleepHz2), 10);
    c.sleepBand    = limitf(0.2, argF(r, "sleepBand", c.sleepBand), 10);
    c.sleepBoost   = limitf(0, argF(r, "sleepBoost", c.sleepBoost), 30);
    c.boostMaxS    = limitf(5, argF(r, "boostMaxS", c.boostMaxS), 600);
    c.wakeDrop     = limitf(0.5, argF(r, "wakeDrop", c.wakeDrop), 30);
    c.sleepMinS    = limitf(0, argF(r, "sleepMinS", c.sleepMinS), 600);
    c.idleGPM      = limitf(0, argF(r, "idleGPM", c.idleGPM), 100);
    c.wakeGPM      = limitf(0, argF(r, "wakeGPM", c.wakeGPM), 100);
    c.stageUpPsi   = limitf(0.5, argF(r, "stageUpPsi", c.stageUpPsi), 30);
    c.stageUpDlyS  = limitf(1, argF(r, "stageUpDlyS", c.stageUpDlyS), 600);
    c.stageDownHz  = limitf(20, argF(r, "stageDownHz", c.stageDownHz), 60);
    c.stageDownDlyS= limitf(1, argF(r, "stageDownDlyS", c.stageDownDlyS), 600);
    c.lagMinRunS   = limitf(0, argF(r, "lagMinRunS", c.lagMinRunS), 3600);
    c.chargeRampS  = limitf(0, argF(r, "chargeRampS", c.chargeRampS), 120);
    c.sleepHzMargin= limitf(0.05, argF(r, "sleepHzMargin", c.sleepHzMargin), 15);
    c.sleepRelShutoff = argF(r, "sleepRelShutoff", c.sleepRelShutoff ? 1 : 0) > 0.5f;
    S.useFlow      = argF(r, "useFlow", S.useFlow ? 1 : 0) > 0.5f;
    S.qMax60       = limitf(1, argF(r, "qMax60", S.qMax60), 5000);
    S.cavOnsetGPM  = limitf(1, argF(r, "cavOnsetGPM", S.cavOnsetGPM), 5000);
    pc.cfg = c;
    plant.qMax60       = S.qMax60;
    plant.shutoffPsi60 = c.shutoffPsiAt60;
    saveSettings();
    r->send(200, "text/plain", pc.capTableOK
        ? "Saved."
        : "Saved -- cap table NOT monotonic (or row 1 <= Min Hz); cap parked at row 1.");
  });

  server.on("/sim", HTTP_POST, [](AsyncWebServerRequest *r) {
    S.simMode      = (int)limitf(0, argF(r, "simMode", S.simMode), 1);
    S.simPsi       = limitf(0, argF(r, "simPsi", S.simPsi), 300);
    S.simDemandGPM = limitf(0, argF(r, "simDemandGPM", S.simDemandGPM), 400);
    S.simTimeScale = limitf(1, argF(r, "simTimeScale", S.simTimeScale), 60);
    S.simCapGalPsi = limitf(0.1, argF(r, "simCapGalPsi", S.simCapGalPsi), 50);
    plant.demandGPM = S.simDemandGPM;
    plant.capacityGalPsi = S.simCapGalPsi;
    saveSettings();
    r->send(200, "text/plain", "Plant updated.");
  });

  server.on("/cmd", HTTP_POST, [](AsyncWebServerRequest *r) {
    String c = r->hasParam("c") ? r->getParam("c")->value() : "";
    if      (c == "start") { gEnable = true;  r->send(200, "text/plain", "Enabled."); }
    else if (c == "stop")  { gEnable = false; r->send(200, "text/plain", "Stopped."); }
    else if (c == "reset") { gCmdReset = millis(); r->send(200, "text/plain", "Reset pulse queued."); }
    else if (c == "scan")  {
#if SIM
      r->send(200, "text/plain", "SIM build -- no bus to scan.");
#else
      lastLog = discover();
      r->send(200, "text/plain", lastLog);
#endif
    }
    else r->send(400, "text/plain", "unknown cmd");
  });

  server.onNotFound([](AsyncWebServerRequest *r) { r->redirect("http://192.168.4.1/"); });
  // ---- network -------------------------------------------------------
  server.on("/net", HTTP_GET, [](AsyncWebServerRequest *r) {
    r->send(200, "application/json", netStatusJSON());
  });

  // On-demand only.  A scan takes the radio off the AP channel for a moment.
  server.on("/scan", HTTP_GET, [](AsyncWebServerRequest *r) {
    r->send(200, "application/json", netScanJSON());
  });

  // Stages the change; the net task applies it.  WiFi calls must not be made
  // from the AsyncTCP task while the net task may be mid-reconnect.
  server.on("/net", HTTP_POST, [](AsyncWebServerRequest *r) {
    NetCfg c = netCurrent();
    argS(r, "ssid",  c.ssid,  sizeof(c.ssid));
    argS(r, "host",  c.host,  sizeof(c.host));
    argS(r, "user",  c.user,  sizeof(c.user));
    argS(r, "topic", c.topic, sizeof(c.topic));
    // A blank password field means "keep the stored one", so a saved network
    // survives an edit to any other field on the page.
    if (r->hasParam("pass", true)  && r->getParam("pass", true)->value().length())
      argS(r, "pass",  c.pass,  sizeof(c.pass));
    if (r->hasParam("mpass", true) && r->getParam("mpass", true)->value().length())
      argS(r, "mpass", c.mpass, sizeof(c.mpass));
    c.port   = (uint16_t)limitf(1, argF(r, "port", c.port), 65535);
    c.pubMs  = (uint16_t)limitf(500, argF(r, "pubMs", c.pubMs), 60000);
    c.mqttOn = argF(r, "mqttOn", c.mqttOn ? 1 : 0) > 0.5f;
    netStage(c);
    r->send(200, "text/plain", c.ssid[0]
        ? "Saved -- joining network, check status in a few seconds."
        : "Saved -- no SSID set, station radio idle.");
  });

  server.begin();
}

// ---------------------------------------------------------------- setup/loop
void setup() {
  Serial.begin(115200);
  pinMode(RS485_DE, OUTPUT); digitalWrite(RS485_DE, LOW);
  Serial1.begin(DRIVE_BAUD, SERIAL_8N1, RS485_RX, RS485_TX);
  rtu.begin(&Serial1, RS485_DE);

  loadSettings();
  pc.reset();
  plant.demandGPM = S.simDemandGPM;
  plant.capacityGalPsi = S.simCapGalPsi;
  plant.psi = 0;

  for (int i = 0; i < MAX_DRIVES; i++) drv[i].addr = FIRST_ADDR + i;

  // AP_STA, not AP.  The config AP stays up permanently so a tech can always
  // reach the page even when the customer network is down, misconfigured, or
  // its password just changed.  One radio serves both: when the station side
  // associates, the softAP follows it to that channel and anything joined to
  // the AP is briefly dropped.  Expected -- provision, then rejoin.
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID, AP_PASS);
  dns.start(53, "*", WiFi.softAPIP());
  setupWeb();
  netBegin();

#if SIM
  Serial.println("############################################################");
  Serial.println("##  SIM BUILD -- simulated plant, no drive is being driven ##");
  Serial.println("##  set  #define SIM 0  for real hardware                  ##");
  Serial.println("############################################################");
  for (int i = 0; i < MAX_DRIVES; i++) drv[i].present = true;
#else
  delay(3000);                      // drives boot slower than this board
  lastLog = discover();
  Serial.print(lastLog);
#endif

  Serial.println("PumpSaver firmware " FW_VERSION);
  Serial.printf("AP %s up at %s\n", AP_SSID, WiFi.softAPIP().toString().c_str());
  Serial.println("t_ms,psi,spAct,hzCmd,cap,shutoff,flow,state,lead,lag,sleep");
}

void loop() {
  dns.processNextRequest();

  uint32_t now = millis();
  if (now - lastTick < TICK_MS) return;
  float dt = (lastTick == 0) ? (TICK_MS / 1000.0f) : (now - lastTick) / 1000.0f;
  lastTick = now;
  tickN++;

  // compress time so a 60 s sleep hold can be watched in a few seconds
  float dtc = dt;
#if SIM
  dtc = dt * S.simTimeScale;
#endif

  pin_.enable    = gEnable && !gLockout;
  pin_.setpoint  = S.setpoint;
  pin_.flowValid = S.useFlow;
  pin_.lagAvail  = drv[1].present && (SIM || (drv[1].commsOK && !drv[1].tripped));
  bool reset = (gCmdReset && now - gCmdReset < 1000);

#if SIM
  // Co-simulate control and plant in small fixed steps.  Integrating the whole
  // tick in one Euler step is unstable: this plant is stiff near shutoff (at
  // 55 psi a 0.4 Hz change swings delivery from 2 to 16 gpm), and at time
  // scale x60 a single 6 s step overshoots setpoint by 300+ psi instead of
  // settling at 60.  Verified in test/harness.cpp.
  const float SUB = 0.01f;
  int steps = (int)ceilf(dtc / SUB);
  if (steps < 1)   steps = 1;
  if (steps > 600) steps = 600;
  float h = dtc / steps;
  for (int k = 0; k < steps; k++) {
    pin_.psi       = plant.psi;
    pin_.psiValid  = true;
    pin_.flowGPM   = plant.flowGPM;
    pc.step(pin_, pout, h);
    plant.step(pout.hzCmd, (pout.runLead ? 1 : 0) + (pout.runLag ? 1 : 0), h);
    // Pressure-hold mode: the header is an infinitely stiff source at whatever
    // the operator dialled in, so whatever the pump just did to it is discarded.
    if (S.simMode == 1) plant.psi = S.simPsi;
  }
  gPsiRaw = plant.psi; gPsiValid = true;

  drv[0].running = pout.runLead; drv[0].hz = pout.runLead ? plant.hzAct : 0;
  drv[1].running = pout.runLag;  drv[1].hz = pout.runLag  ? plant.hzAct : 0;
  drv[0].amps = pout.runLead ? plant.amps : 0;
  drv[1].amps = pout.runLag  ? plant.amps : 0;
  drv[0].commsOK = drv[1].commsOK = true;
#else
  uint16_t ai = 0;
  Drive &src = drv[0];                       // transducer lives on the lead drive
  if (src.present && rtu.readHolding(src.addr, REG(20), 1, &ai) && ai <= 1000) {
    gPsiRaw   = ai / 1000.0f * S.xdcrSpanPsi;
    // The drive's own P-16 = "t 4-20" is what catches a broken loop; this only
    // catches an impossible count or a dead/tripped source.  P-16 cannot be
    // read back over Modbus, so that setting stays an unverified keypad step.
    gPsiValid = src.commsOK && !src.tripped;
  } else {
    gPsiValid = false;
  }

  pin_.psi      = gPsiRaw;
  pin_.psiValid = gPsiValid;
  pc.step(pin_, pout, dtc);

  // Same speed to every running pump -- parallel pumps on a common discharge
  // header must match, or the slower one sits below header pressure and churns.
  commandDrive(drv[0], pout.runLead, pout.runLead ? pout.hzCmd : 0, reset);
  commandDrive(drv[1], pout.runLag,  pout.runLag  ? pout.hzCmd : 0, reset);
  if (tickN % STATUS_EVERY == 0) { pollDrive(drv[0]); pollDrive(drv[1]); }
#endif

  // ---- 4. CSV for the bench
  Serial.printf("%lu,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%s,%d,%d,%d\n",
    now, pin_.psi, pout.spActive, pout.hzCmd, pout.capHz, pout.shutoffHz,
    plant.flowGPM, stateName(pout.state), pout.runLead, pout.runLag, pc.sleepStage);

  // ---- 5. hand a snapshot to the net task.  Copy only; it publishes on its
  //         own schedule and on its own core, so nothing here can block.
  NetTelem tl;
  tl.psi     = pin_.psi;      tl.spAct   = pout.spActive;
  tl.hzCmd   = pout.hzCmd;    tl.cap     = pout.capHz;
  tl.shutoff = pout.shutoffHz; tl.flow   = plant.flowGPM;
  tl.state   = (int)pout.state; tl.sleepStage = pc.sleepStage;
  tl.enable  = pin_.enable;   tl.runLead = pout.runLead;
  tl.runLag  = pout.runLag;   tl.commsOK = drv[0].commsOK;
  tl.psiValid = gPsiValid;    tl.upSec   = now / 1000;
  netPushTelem(tl);
}
