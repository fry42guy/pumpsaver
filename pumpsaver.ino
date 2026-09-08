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

// ---------------------------------------------------------------- version
// Bump FW_VERSION on every change pass before flashing.  The number is shown
// on the serial banner, in the page header and in /status, so a board in the
// field can always be matched to a commit.  See VERSION.md for the log.
// This block must stay ABOVE FW_VERSION_STR below -- that initialiser expands
// the macro, so defining it afterwards does not compile.
#define FW_VERSION "0.14.0"

// net.h needs the version string at runtime; the macro is not visible to it.
const char *FW_VERSION_STR = FW_VERSION;
#include "net.h"

// ---------------------------------------------------------------- build mode
// 1 = simulated plant, no drive or RS485 needed.  0 = real drives over Modbus.
// The web page shows an unmissable banner while this is 1, and /status and
// /sys both report it, so a screenshot can never be mistaken for real data.
//
// Set from build.ps1 rather than by editing this line:
//     .\build.ps1 -Sim -Upload     simulated plant, for a demo or the bench
//     .\build.ps1 -Upload          real drives over Modbus
// Both binaries therefore come from ONE commit, which is what makes a board in
// the field matchable to a commit -- a hand-edited define does not.
// build.ps1 writes sim_flag.h on every run (it is gitignored, and generated,
// so it never disagrees with the last build).  __has_include keeps a plain
// Arduino IDE build working when the file is not there: no file, real drives.
//
// Passing -DSIM=1 via --build-property compiler.cpp.extra_flags was tried and
// does not work: it changes the core cache key in a way that drops symbols
// from the Arduino core, and AsyncTCP then fails to link with "undefined
// reference to micros".
#if defined(__has_include)
#  if __has_include("sim_flag.h")
#    include "sim_flag.h"
#  endif
#endif
#ifndef SIM
#define SIM 0
#endif

// AP SSID and password are no longer compiled in -- they live in NetCfg (net.h,
// its own NVS namespace) so they can be changed from the Network screen without
// a reflash. netBegin() raises the AP. Factory defaults: FCW-PUMP / fullcircle.
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
bool gwOwnsDrive();            // can_gw.h -- PLC owns the drive over CAN

struct Drive {
  uint8_t  addr    = 0;
  bool     present = false;      // found by discovery, remembered afterwards
  bool     commsOK = false;
  int      fails   = 0;
  uint16_t status  = 0;
  float    hz = 0, amps = 0;
  bool     running = false, tripped = false, ready = false;
  uint8_t  tripCode = 0;
  // Writes are tracked separately from reads.  "The drive answers a read" and
  // "the drive accepts a write" are different questions -- a wrong P-12, a
  // write-protected parameter set or a read-only slave answers one and not the
  // other -- and a discarded write result makes a dead command path look
  // exactly like a working one.
  bool     writeOK = false;
  uint32_t writeFails = 0;

  // ---- registers beyond the status triplet, carried so the UI can show what
  // the drive actually reports rather than what we inferred.
  float    tempC     = 0;      // reg 24, heatsink degC
  uint16_t aiCounts  = 0;      // reg 20, 0-1000 = 0-100 % of the 4-20 mA span

  // Injected fault, simulation only.  Non-zero puts this trip code into the
  // synthesised status word, so the sim exercises the SAME decode path as a
  // real drive instead of a special case.  Cleared by a fault reset.
  uint8_t  simTrip   = 0;
};

// Invertek status word: bit0 running, bit1 tripped, bit6 ready, high byte the
// trip code.  Built here so the simulated path and the real path decode
// identically -- a sim that bypasses the decode proves nothing about it.
static inline uint16_t buildStatus(bool running, uint8_t trip, bool ready) {
  uint16_t s = 0;
  if (running) s |= 0x01;
  if (trip)    s |= 0x02;
  if (ready)   s |= 0x40;
  return s | ((uint16_t)trip << 8);
}

static inline void decodeStatus(Drive &d) {
  d.running  = d.status & 0x01;
  d.tripped  = d.status & 0x02;
  d.ready    = d.status & 0x40;
  d.tripCode = d.status >> 8;
}
Drive drv[MAX_DRIVES];
bool  addr1Occupied = false;     // an uncommissioned drive is sitting at 1

// Present the number of pumps the operator says the simulated skid has, so a
// single-pump skid can be exercised without the staging logic being offered a
// lag pump that does not exist.
void applySimDrives();           // defined once Settings is in scope

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

  /* ---- appended in 0.11.0 -------------------------------------------------
     APPEND ONLY, same reasoning as NetCfg: the blob is a raw byte copy and
     Preferences::getBytes fills only as many bytes as were stored, so fields
     added at the END read as the defaults below on a board configured by an
     older build.  The magic therefore does not change and nobody loses a
     tuned cap table to a UI feature.

     simOn makes simulation a RUNTIME mode.  It is persisted -- a demo board
     should still be a demo board after a power cut -- but gEnable is not, so
     a reboot in simulation always comes back stopped.  The compile-time SIM
     macro is now only the factory default for a board with blank NVS.       */
  bool     simOn         = SIM;
  int      simDrives     = 2;        // 1 = lead only, 2 = lead + lag

  /* ---- appended in 0.13.0 -- pump identity and motor nameplate ------------
     APPEND ONLY, same reasoning as above.  None of this reaches the control
     block; it exists so a set of tuning numbers identifies a real machine,
     and so measured amps can be checked against the FLA the motor is rated
     for rather than the drive's own rating.  See pump_profile.h.           */
  char     pumpName[28]  = "";
  float    motorHp       = 0;
  float    motorVolts    = 0;
  float    motorFLA      = 0;        // nameplate full load amps
  float    motorRPM      = 0;
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

// Diagnostic max-Hz override.  NOT persisted, for the same reason gEnable is
// not: it replaces the cavitation cap, so a reboot has to come back with the
// cap enforcing again rather than silently still overridden.
bool     gOvr     = false;
float    gOvrPct  = 100.0f;      // 0-100 %, of 60 Hz
uint32_t gCmdReset = 0;
uint32_t gRebootAt = 0;          // set by /reboot; 0 = no restart pending
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
void applySimDrives() {
  for (int i = 0; i < MAX_DRIVES; i++) {
    bool on = S.simOn && (i < S.simDrives);
    drv[i].present  = on;
    drv[i].commsOK  = on;
    drv[i].writeOK  = on;
    drv[i].addr     = FIRST_ADDR + i;
    if (!on) { drv[i].running = false; drv[i].hz = 0; drv[i].amps = 0;
               drv[i].tripped = false; drv[i].tripCode = 0; }
  }
}

void saveSettings() {
  S.c = pc.cfg;
  prefs.begin("pumpsaver", false);
  prefs.putBytes("blob", &S, sizeof(S));
  prefs.end();
}

// ---------------------------------------------------------------- drive i/o
// Compiled into EVERY build now that simulation is a runtime mode rather than
// a compile-time one.  Flash is at 37% of a 3 MB partition, so carrying both
// paths costs nothing worth counting.
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
    decodeStatus(d);
    // Heatsink temperature and the raw analogue count are separate reads and
    // are not worth a bus transaction every poll -- every fourth is plenty for
    // a thermal reading, and it keeps the tick cheap.
    uint16_t x;
    if ((d.fails == 0) && (tickN % (STATUS_EVERY * 4) == 0)) {
      if (rtu.readHolding(d.addr, REG(24), 1, &x) && x <= 150) d.tempC = x;
      if (rtu.readHolding(d.addr, REG(20), 1, &x) && x <= 1000) d.aiCounts = x;
    }
  } else if (++d.fails >= 3) {
    d.commsOK = false;
  }
}

void commandDrive(Drive &d, bool run, float hz, bool reset) {
  if (!d.present) return;
  // In gateway mode the PLC owns this drive: its speed reference arrives on
  // CAN and is written by can_gw.h. Two writers on one Modbus link would
  // fight at whatever rate each happens to run, so the local loop stands
  // down. Declared here rather than at the call site because every path
  // that commands a drive goes through this function.
  if (gwOwnsDrive()) return;
  uint16_t w[2];
  w[0] = (run ? 1 : 0) | (reset ? 4 : 0);
  w[1] = (uint16_t)(limitf(0, hz, 60.0f) * 10.0f + 0.5f);
  bool ok = rtu.writeMultiple(d.addr, REG(1), w, 2);
  if (ok != d.writeOK)
    Serial.printf("drive at %u: writes %s\n", d.addr, ok ? "OK" : "FAILING");
  d.writeOK = ok;
  if (!ok) d.writeFails++;
}

// ---------------------------------------------------------------- web page
// Quote and escape a free-text field.  Only the pump name and profile names
// are operator-typed, but one stray quote in a name would otherwise produce
// JSON the page cannot parse, and the whole UI goes blank on a typo.
String jstr(const char *s) {
  String o = "\"";
  for (const char *p = s; *p; p++) {
    if (*p == '"' || *p == '\\') { o += '\\'; o += *p; }
    else if ((uint8_t)*p < 0x20)  o += ' ';
    else                          o += *p;
  }
  return o + "\"";
}

#include "config_io.h"
#include "pump_profile.h"   // reuses the cfg* reader above
#include "can_gw.h"
#include "page.h"

// ---------------------------------------------------------------- json

float flowAt(float hz, float psi);   // defined below; .ino prototype
                                     // generation is not worth relying on

// Identical pumps in parallel at the same speed against the same head each
// deliver the same flow, so system delivery is n x the single-pump curve.
// flowAt() is per pump: publishing it raw read 35 gpm with two pumps staged
// and 70 gpm actually flowing.
float flowTotal() {
  int n = (pout.runLead ? 1 : 0) + (pout.runLag ? 1 : 0);
  return n * flowAt(pout.hzCmd, pin_.psi);
}

String statusJSON() {
  String j = "{";
  j += "\"ver\":\"" FW_VERSION "\"";
  j += ",\"sim\":" + String(S.simOn ? "true" : "false");
  j += ",\"simDrives\":" + String(S.simDrives);
  j += ",\"psi\":" + String(pin_.psi, 1) + ",\"psiValid\":" + String(gPsiValid ? "true" : "false");
  j += ",\"spActive\":" + String(pout.spActive, 1) + ",\"hzCmd\":" + String(pout.hzCmd, 1);
  j += ",\"capHz\":" + String(pout.capHz, 1) + ",\"shutoffHz\":" + String(pout.shutoffHz, 1);
  j += ",\"state\":" + String(pout.state) + ",\"enable\":" + String(gEnable ? "true" : "false");
  j += ",\"ovr\":" + String(gOvr ? "true" : "false") + ",\"ovrPct\":" + String(gOvrPct, 0);
  j += ",\"sleepCycles\":" + String(pc.sleepCycles);
  // Controller uptime, not the browser's -- a reader has to be able to tell
  // that the board rebooted under them.  Seconds since boot.
  j += ",\"up\":" + String(millis() / 1000UL);
  j += ",\"addr1\":" + String(addr1Occupied ? "true" : "false");
  j += ",\"flowEst\":" + String(flowTotal(), 1);
  j += ",\"flowSim\":" + String(plant.flowGPM, 1) + ",\"hzAct\":" + String(plant.hzAct, 1);
  j += ",\"drives\":[";
  for (int i = 0; i < MAX_DRIVES; i++) {
    if (i) j += ",";
    j += "{\"n\":" + String(i + 1) + ",\"addr\":" + String(drv[i].addr);
    j += ",\"present\":" + String(drv[i].present ? "true" : "false");
    j += ",\"commsOK\":" + String(drv[i].commsOK ? "true" : "false");
    j += ",\"writeOK\":" + String(drv[i].writeOK ? "true" : "false");
    j += ",\"writeFails\":" + String(drv[i].writeFails);
    j += ",\"hz\":" + String(drv[i].hz, 1) + ",\"amps\":" + String(drv[i].amps, 1);
    j += ",\"running\":" + String(drv[i].running ? "true" : "false");
    j += ",\"tripped\":" + String(drv[i].tripped ? "true" : "false");
    j += ",\"tripCode\":" + String(drv[i].tripCode);
    j += ",\"status\":" + String(drv[i].status);
    j += ",\"ready\":" + String(drv[i].ready ? "true" : "false");
    j += ",\"tempC\":" + String(drv[i].tempC, 0);
    j += ",\"ai\":" + String(drv[i].aiCounts) + "}";
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
  j += ",\"pumpName\":" + jstr(S.pumpName);
  j += ",\"motorHp\":" + String(S.motorHp, 1) + ",\"motorVolts\":" + String(S.motorVolts, 0);
  j += ",\"motorFLA\":" + String(S.motorFLA, 1) + ",\"motorRPM\":" + String(S.motorRPM, 0);
  j += ",\"capTableOK\":" + String(pc.capTableOK ? "true" : "false");
  j += ",\"simOn\":" + String(S.simOn ? 1 : 0);
  j += ",\"simDrives\":" + String(S.simDrives);
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

// Live readings must never be cached.  None of these endpoints sent any
// cache header, so a client was free to serve a stale copy -- and clients do.
// On /status that means a page showing a pressure that is seconds old with no
// indication, which is indistinguishable from a controller that has stopped
// updating.  Anything that reports state gets no-store.
void sendNoCache(AsyncWebServerRequest *r, int code, const char *type, const String &body) {
  AsyncWebServerResponse *res = r->beginResponse(code, type, body);
  res->addHeader("Cache-Control", "no-store, no-cache, must-revalidate");
  r->send(res);
}

void setupWeb() {
  server.on("/",        HTTP_GET, [](AsyncWebServerRequest *r) { r->send(200, "text/html", PAGE_HOME); });
  server.on("/pump",    HTTP_GET, [](AsyncWebServerRequest *r) { r->send(200, "text/html", PAGE_PUMP); });
  server.on("/sim",     HTTP_GET, [](AsyncWebServerRequest *r) { r->send(200, "text/html", PAGE_SIM); });
  server.on("/network", HTTP_GET, [](AsyncWebServerRequest *r) { r->send(200, "text/html", PAGE_NET); });
  server.on("/system",  HTTP_GET, [](AsyncWebServerRequest *r) { r->send(200, "text/html", PAGE_SYSTEM); });
  server.on("/status", HTTP_GET, [](AsyncWebServerRequest *r) { sendNoCache(r, 200, "application/json", statusJSON()); });
  server.on("/settings", HTTP_GET, [](AsyncWebServerRequest *r) { sendNoCache(r, 200, "application/json", settingsJSON()); });
  server.on("/diag", HTTP_GET, [](AsyncWebServerRequest *r) { sendNoCache(r, 200, "application/json", diagJSON()); });
  server.on("/log", HTTP_GET, [](AsyncWebServerRequest *r) { sendNoCache(r, 200, "text/plain", lastLog); });

  server.on("/set", HTTP_POST, [](AsyncWebServerRequest *r) {
    // Read raw, then clamp once through cfgClamp(). The limits live in exactly
    // one place (config_io.h) so this path and import can never disagree about
    // what a legal value is.
    PumpCfg c = pc.cfg;
    S.setpoint     = argF(r, "setpoint", S.setpoint);
    S.xdcrSpanPsi  = argF(r, "xdcrSpanPsi", S.xdcrSpanPsi);
    S.qMax60       = argF(r, "qMax60", S.qMax60);
    S.cavOnsetGPM  = argF(r, "cavOnsetGPM", S.cavOnsetGPM);
    if (r->hasParam("pumpName", true)) argS(r, "pumpName", S.pumpName, sizeof(S.pumpName));
    S.motorHp      = limitf(0, argF(r, "motorHp",    S.motorHp),    500);
    S.motorVolts   = limitf(0, argF(r, "motorVolts", S.motorVolts), 1000);
    S.motorFLA     = limitf(0, argF(r, "motorFLA",   S.motorFLA),   500);
    S.motorRPM     = limitf(0, argF(r, "motorRPM",   S.motorRPM),   10000);
    S.useFlow      = argF(r, "useFlow", S.useFlow ? 1 : 0) > 0.5f;
    c.kp           = argF(r, "kp", c.kp);
    c.ki           = argF(r, "ki", c.ki);
    c.minHz        = argF(r, "minHz", c.minHz);
    c.maxHz        = argF(r, "maxHz", c.maxHz);
    c.spRampPsiS   = argF(r, "spRampPsiS", c.spRampPsiS);
    c.spStepPsi    = argF(r, "spStepPsi", c.spStepPsi);
    c.shutoffPsiAt60 = argF(r, "shutoffPsiAt60", c.shutoffPsiAt60);
    c.capPts       = (int)argF(r, "capPts", c.capPts);
    c.capEnable    = argF(r, "capEnable", c.capEnable ? 1 : 0) > 0.5f;
    for (int i = 0; i < 4; i++) {
      c.capPsi[i]  = argF(r, (String("cp") + i).c_str(), c.capPsi[i]);
      c.capHzPt[i] = argF(r, (String("ch") + i).c_str(), c.capHzPt[i]);
    }
    c.sleepDlyS    = argF(r, "sleepDlyS", c.sleepDlyS);
    c.sleepHz      = argF(r, "sleepHz", c.sleepHz);
    c.sleepHz2     = argF(r, "sleepHz2", c.sleepHz2);
    c.sleepHzMargin= argF(r, "sleepHzMargin", c.sleepHzMargin);
    c.sleepRelShutoff = argF(r, "sleepRelShutoff", c.sleepRelShutoff ? 1 : 0) > 0.5f;
    c.sleepBand    = argF(r, "sleepBand", c.sleepBand);
    c.sleepBoost   = argF(r, "sleepBoost", c.sleepBoost);
    c.boostMaxS    = argF(r, "boostMaxS", c.boostMaxS);
    c.chargeRampS  = argF(r, "chargeRampS", c.chargeRampS);
    c.wakeDrop     = argF(r, "wakeDrop", c.wakeDrop);
    c.sleepMinS    = argF(r, "sleepMinS", c.sleepMinS);
    c.idleGPM      = argF(r, "idleGPM", c.idleGPM);
    c.wakeGPM      = argF(r, "wakeGPM", c.wakeGPM);
    c.stageUpPsi   = argF(r, "stageUpPsi", c.stageUpPsi);
    c.stageUpDlyS  = argF(r, "stageUpDlyS", c.stageUpDlyS);
    c.stageDownHz  = argF(r, "stageDownHz", c.stageDownHz);
    c.stageDownDlyS= argF(r, "stageDownDlyS", c.stageDownDlyS);
    c.lagMinRunS   = argF(r, "lagMinRunS", c.lagMinRunS);

    cfgClamp(S, c);
    pc.cfg = c;
    plant.qMax60       = S.qMax60;
    plant.shutoffPsi60 = c.shutoffPsiAt60;
    saveSettings();
    r->send(200, "text/plain", pc.capTableOK
        ? "Saved."
        : "Saved -- cap table NOT monotonic (or row 1 <= Min Hz); cap parked at row 1.");
  });

  server.on("/sim", HTTP_POST, [](AsyncWebServerRequest *r) {
    bool wasSim = S.simOn;
    if (r->hasParam("on", true)) S.simOn = argF(r, "on", S.simOn ? 1 : 0) > 0.5f;
    S.simDrives = (int)limitf(1, argF(r, "drives", S.simDrives), MAX_DRIVES);

    if (S.simOn != wasSim) {
      // Entering simulation, the loop stops feeding real pressure to the
      // controller -- so it must also stop commanding real pumps.  Drop the
      // run demand and write stop/0 Hz to anything actually on the bus before
      // switching, rather than going silent and leaving the drive's P-36
      // watchdog to trip it.  A trip would stop the pump too, but it would
      // stop it as a FAULT that somebody then has to go and reset.
      gEnable = false;
      if (S.simOn)
        for (int i = 0; i < MAX_DRIVES; i++)
          if (drv[i].present) commandDrive(drv[i], false, 0, false);
      pc.reset();
      plant.psi = 0;
      // Leaving simulation, the drive list is whatever discovery finds, not
      // whatever the sim was pretending to have.
      if (!S.simOn) lastLog = discover();
    }
    // Unconditional while simulating, not just on the on/off edge: changing
    // the pump count from 2 to 1 has to take effect now, and gating this on
    // the edge meant a count change did nothing until the next reboot.
    if (S.simOn) applySimDrives();

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
    else if (c == "reset") {
      gCmdReset = millis();               // real path: pulse the drive's reset bit
      // Simulation: clear the injected trip HERE, not from the tick.  Doing it
      // inside the 1 s pulse window raced with this handler -- a /fault posted
      // while the window was still open was wiped before it was ever reported,
      // and a reset issued just after one could miss the window entirely.  The
      // pulse exists because a real drive needs the bit held; a simulated trip
      // is just a variable, so set it where it is asked for.
      if (S.simOn) { drv[0].simTrip = 0; drv[1].simTrip = 0; }
      r->send(200, "text/plain", S.simOn ? "Trips cleared." : "Reset pulse queued.");
    }
    else if (c == "scan")  {
      if (S.simOn) { r->send(200, "text/plain", "Simulation is on -- there is no bus to scan."); }
      else { lastLog = discover(); sendNoCache(r, 200, "text/plain", lastLog); }
    }
    else r->send(400, "text/plain", "unknown cmd");
  });

  // Diagnostic max-Hz override.  Nothing here is written to NVS -- see gOvr.
  // Inject or clear a drive trip.  Simulation only -- there is no way to make
  // a real drive trip on command, and pretending otherwise would put a fake
  // fault code on a screen next to real ones.
  server.on("/fault", HTTP_POST, [](AsyncWebServerRequest *r) {
    if (!S.simOn) { r->send(409, "text/plain", "Only while simulating."); return; }
    int n    = (int)limitf(1, argF(r, "drive", 1), MAX_DRIVES);
    int code = (int)limitf(0, argF(r, "code", 0), 255);
    drv[n - 1].simTrip = (uint8_t)code;
    r->send(200, "text/plain", code
        ? "Drive " + String(n) + " tripped, code " + String(code) + "."
        : "Drive " + String(n) + " trip cleared.");
  });

  server.on("/ovr", HTTP_POST, [](AsyncWebServerRequest *r) {
    if (r->hasParam("on", true))
      gOvr = r->getParam("on", true)->value() == "1";
    if (r->hasParam("pct", true))
      gOvrPct = limitf(0, r->getParam("pct", true)->value().toFloat(), 100);
    r->send(200, "application/json",
            String("{\"ovr\":") + (gOvr ? "true" : "false") +
            ",\"ovrPct\":" + String(gOvrPct, 0) + "}");
  });

  // What is running, for the System screen.  Cheap enough to poll.
  server.on("/sys", HTTP_GET, [](AsyncWebServerRequest *r) {
    String j = "{\"ver\":\"" FW_VERSION "\",\"id\":\"" + String(gDevId) + "\"";
    j += ",\"sim\":" + String(S.simOn ? "true" : "false");
    j += ",\"up\":" + String(millis() / 1000UL);
    j += ",\"reset\":\"" + String(esp_reset_reason() == ESP_RST_POWERON  ? "power on"
                               : esp_reset_reason() == ESP_RST_SW        ? "software"
                               : esp_reset_reason() == ESP_RST_PANIC     ? "panic"
                               : esp_reset_reason() == ESP_RST_TASK_WDT  ? "task watchdog"
                               : esp_reset_reason() == ESP_RST_INT_WDT   ? "int watchdog"
                               : esp_reset_reason() == ESP_RST_BROWNOUT  ? "brownout"
                                                                        : "other") + "\"";
    j += ",\"heap\":" + String(ESP.getFreeHeap());
    j += ",\"psram\":" + String(ESP.getFreePsram());
    j += ",\"sketch\":" + String(ESP.getSketchSize());
    j += ",\"appSpace\":" + String(ESP.getSketchSize() + ESP.getFreeSketchSpace());
    j += ",\"flash\":" + String(ESP.getFlashChipSize());
    j += ",\"mhz\":" + String(getCpuFrequencyMhz());
    j += "}";
    sendNoCache(r, 200, "application/json", j);
  });

  // Restart, requested from the Network screen after an AP change.  The flag
  // is consumed in loop() so the response is on the wire before the radio
  // drops -- restarting inside the handler gives the browser a dead socket.
  server.on("/reboot", HTTP_POST, [](AsyncWebServerRequest *r) {
    gRebootAt = millis() + 800;
    r->send(200, "text/plain", "Rebooting.");
  });

  // Captive-portal catch-all, but ONLY for requests that arrived on the AP.
  // Bouncing everything to the softAP address was fine when that was the only
  // way in; now that the board is also a station, it would send a mistyped URL
  // on the site network to 192.168.4.1, which is unreachable from there.  On
  // the AP an absolute URL is still required, because the OS connectivity
  // check asks for a URL on somebody else's host.
  server.onNotFound([](AsyncWebServerRequest *r) {
    IPAddress ap = WiFi.softAPIP();
    if (ap != IPAddress((uint32_t)0) && r->client()->localIP() == ap)
      r->redirect("http://" + ap.toString() + "/");
    else
      r->send(404, "text/plain", "not found");
  });
  // ---- network -------------------------------------------------------
  server.on("/net", HTTP_GET, [](AsyncWebServerRequest *r) {
    sendNoCache(r, 200, "application/json", netStatusJSON());
  });

  // On-demand only.  A scan takes the radio off the AP channel for a moment.
  server.on("/scan", HTTP_GET, [](AsyncWebServerRequest *r) {
    sendNoCache(r, 200, "application/json", netScanJSON());
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
    c.tbMode = argF(r, "tbMode", c.tbMode ? 1 : 0) > 0.5f;
    // Only touched when the form actually sent it.  argF's default would map a
    // missing param to the current value anyway, but a mask is the one field
    // where a silent 0 means "publish nothing" -- worth being explicit.
    if (r->hasParam("pubMask", true))
      c.pubMask = (uint32_t)strtoul(r->getParam("pubMask", true)->value().c_str(), nullptr, 10);

    // ---- identity, AP and addressing (0.10.0) ----
    if (r->hasParam("label", true)) argS(r, "label", c.label, sizeof(c.label));
    if (r->hasParam("hostName", true)) {
      String h = netSanitizeHost(r->getParam("hostName", true)->value());
      strncpy(c.host_name, h.c_str(), sizeof(c.host_name) - 1);
      c.host_name[sizeof(c.host_name) - 1] = 0;
    }
    if (r->hasParam("apSsid", true) && r->getParam("apSsid", true)->value().length())
      argS(r, "apSsid", c.apSsid, sizeof(c.apSsid));
    // Blank means "keep the stored one"; shorter than 8 would silently open
    // the AP, so it is refused rather than accepted and quietly downgraded.
    if (r->hasParam("apPass", true) && r->getParam("apPass", true)->value().length() >= 8)
      argS(r, "apPass", c.apPass, sizeof(c.apPass));

    c.useStatic = argF(r, "useStatic", c.useStatic ? 1 : 0) > 0.5f;
    String badq;
    struct { const char *k; uint8_t *v; } quads[] = {
      {"sip", c.ip}, {"gw", c.gw}, {"mask", c.mask}, {"dns", c.dns} };
    for (auto &q : quads) {
      if (!r->hasParam(q.k, true)) continue;
      String v = r->getParam(q.k, true)->value();
      v.trim();
      if (!v.length()) { memset(q.v, 0, 4); continue; }   // blank = not set
      if (!netParseQuad(v, q.v)) badq += String(" ") + q.k;
    }

    netStage(c);
    if (badq.length()) {
      r->send(200, "text/plain", "Saved, but kept the previous" + badq +
                                 " -- not a valid address.");
      return;
    }
    r->send(200, "text/plain", c.ssid[0]
        ? "Saved -- joining network, check status in a few seconds."
        : "Saved -- no SSID set, station radio idle.");
  });

  // ---- backup / restore ----------------------------------------------
  // Named JSON, not the NVS blob: a file written by this firmware has to stay
  // readable by later firmware.  Passwords are never in it.
  // ---------------------------------------------------------------- profiles
  server.on("/profiles", HTTP_GET, [](AsyncWebServerRequest *r) {
    sendNoCache(r, 200, "application/json", profListJSON());
  });

  server.on("/profile", HTTP_GET, [](AsyncWebServerRequest *r) {
    // The pump block as it stands right now, for export.
    sendNoCache(r, 200, "application/json",
            profJSON(profCapture(S, pc.cfg, S.pumpName[0] ? S.pumpName : "unnamed")));
  });

  server.on("/profile/apply", HTTP_POST, [](AsyncWebServerRequest *r) {
    PumpProfile p;
    if (r->hasParam("json", true)) {
      p = profParse(r->getParam("json", true)->value());
    } else if (r->hasParam("id", true)) {
      if (!profById(r->getParam("id", true)->value(), p)) {
        r->send(404, "text/plain", "No such profile."); return;
      }
    } else { r->send(400, "text/plain", "Need id or json."); return; }

    PumpCfg c = pc.cfg;
    profApply(p, S, c);
    cfgClamp(S, c);                 // one clamp, shared with /set and import
    pc.cfg = c;
    plant.qMax60       = S.qMax60;
    plant.shutoffPsi60 = c.shutoffPsiAt60;
    saveSettings();
    // Applying a pump does NOT touch setpoint, sleep or staging: those belong
    // to the installation, not to the pump.  Say so, so nobody assumes it did.
    r->send(200, "text/plain", String("Applied \"") + p.name +
            "\". Setpoint, sleep and staging left as they were." +
            (pc.capTableOK ? "" : " WARNING: cap table not monotonic."));
  });

  server.on("/profile/save", HTTP_POST, [](AsyncWebServerRequest *r) {
    int slot = (int)limitf(0, argF(r, "slot", 0), PROFILE_SLOTS - 1);
    String nm = r->hasParam("name", true) ? r->getParam("name", true)->value() : String("");
    nm.trim();
    if (!nm.length()) { r->send(400, "text/plain", "Give the profile a name."); return; }
    gProf.slot[slot] = profCapture(S, pc.cfg, nm.c_str());
    gProf.used[slot] = true;
    profSave();
    r->send(200, "text/plain", "Saved to slot " + String(slot + 1) + " as \"" + nm + "\".");
  });

  server.on("/profile/delete", HTTP_POST, [](AsyncWebServerRequest *r) {
    int slot = (int)limitf(0, argF(r, "slot", 0), PROFILE_SLOTS - 1);
    gProf.used[slot] = false;
    profSave();
    r->send(200, "text/plain", "Slot " + String(slot + 1) + " cleared.");
  });

  server.on("/export", HTTP_GET, [](AsyncWebServerRequest *r) {
    String body = cfgExportJSON();
    AsyncWebServerResponse *res = r->beginResponse(200, "application/json", body);
    res->addHeader("Content-Disposition",
                   String("attachment; filename=\"pumpsaver-") + gDevId + "-" FW_VERSION ".json\"");
    r->send(res);
  });

  server.on("/can", HTTP_GET, [](AsyncWebServerRequest *r) {
    sendNoCache(r, 200, "application/json", canStatusJSON());
  });

  server.on("/cantrace", HTTP_GET, [](AsyncWebServerRequest *r) {
    r->send(200, "text/plain", canTraceText());
  });

  // Changing bus role or node ID mid-run is not something to do live, and
  // the TWAI driver is installed once at boot, so this saves and asks for a
  // reboot rather than pretending it took effect.
  server.on("/canset", HTTP_POST, [](AsyncWebServerRequest *r) {
    CanCfg c = canCurrent();
    c.enable     = argF(r, "enable",     c.enable     ? 1 : 0) > 0.5f;
    c.listenOnly = argF(r, "listenOnly", c.listenOnly ? 1 : 0) > 0.5f;
    c.capEnforce = argF(r, "capEnforce", c.capEnforce ? 1 : 0) > 0.5f;
    c.node       = (uint8_t) limitf(1,  argF(r, "node",      c.node),      127);
    c.hbMs       = (uint16_t)limitf(50, argF(r, "hbMs",      c.hbMs),      3000);
    c.pdoMs      = (uint16_t)limitf(20, argF(r, "pdoMs",     c.pdoMs),     1000);
    c.driveAddr  = (uint8_t) limitf(0,  argF(r, "driveAddr", c.driveAddr), 247);
    c.vendor     = (uint32_t)argF(r, "vendor",   c.vendor);
    c.product    = (uint32_t)argF(r, "product",  c.product);
    c.revision   = (uint32_t)argF(r, "revision", c.revision);
    c.serial     = (uint32_t)argF(r, "serial",   c.serial);
    c.devType    = (uint32_t)argF(r, "devType",  c.devType);
    canStage(c);
    r->send(200, "text/plain", "Saved. Reboot to apply the bus settings.");
  });

  server.on("/import", HTTP_POST, [](AsyncWebServerRequest *r) {
    if (!r->hasParam("cfg", true)) { r->send(400, "text/plain", "No config supplied."); return; }
    bool withNet = r->hasParam("withNet", true) &&
                   r->getParam("withNet", true)->value().toInt() > 0;
    lastLog = cfgImportJSON(r->getParam("cfg", true)->value(), withNet);
    sendNoCache(r, 200, "text/plain", lastLog);
  });

  server.begin();
}

// ---------------------------------------------------------------- setup/loop
void setup() {
  Serial.begin(115200);
  // A USB CDC write BLOCKS while the host is not draining the port, and the
  // per-tick CSV line is written from inside the control loop.  Measured on
  // the bench with nothing reading the port: average tick 401 ms instead of
  // 100, with one stall of 24 SECONDS.  A pressure loop that stops for 24 s
  // because a laptop was unplugged is not a pressure loop.
  //
  // Timeout 0 means a write is dropped rather than waited on.  Logging must
  // never be able to stall control -- the CSV is diagnostic, the tick is not.
  Serial.setTxTimeoutMs(0);
  pinMode(RS485_DE, OUTPUT); digitalWrite(RS485_DE, LOW);
  Serial1.begin(DRIVE_BAUD, SERIAL_8N1, RS485_RX, RS485_TX);
  rtu.begin(&Serial1, RS485_DE);

  loadSettings();
  profLoad();                       // saved pump profiles, own NVS namespace
  profLoad();
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
  netBegin();                       // loads NetCfg, raises the AP, starts the net task
  dns.start(53, "*", WiFi.softAPIP());
  setupWeb();

  if (S.simOn) {
    Serial.println("############################################################");
    Serial.println("##  SIMULATION ON -- no drive is being driven             ##");
    Serial.println("##  turn it off on the System screen for real drives      ##");
    Serial.println("############################################################");
    applySimDrives();
  } else {
    delay(3000);                    // drives boot slower than this board
    lastLog = discover();
    Serial.print(lastLog);
  }

  canBegin();

  Serial.println("PumpSaver firmware " FW_VERSION);
  Serial.print(gNetLog); Serial.println();
  if (gMdnsUp) Serial.printf("mDNS: http://%s.local/\n", gNet.host_name);
  Serial.println("t_ms,psi,spAct,hzCmd,cap,shutoff,flow,state,lead,lag,sleep,"
                 "d1Hz,d1A,d1st,d1rd,d1wr,d2Hz,d2A,d2st,d2rd,d2wr");
}

void loop() {
  dns.processNextRequest();

  // A restart requested from the web UI happens here, not in the handler, so
  // the HTTP response reaches the browser before the radio goes down.
  if (gRebootAt && (int32_t)(millis() - gRebootAt) >= 0) ESP.restart();

  uint32_t now = millis();
  if (now - lastTick < TICK_MS) return;
  float dt = (lastTick == 0) ? (TICK_MS / 1000.0f) : (now - lastTick) / 1000.0f;
  lastTick = now;
  tickN++;

  // compress time so a 60 s sleep hold can be watched in a few seconds
  float dtc = S.simOn ? dt * S.simTimeScale : dt;

  pin_.enable    = gEnable && !gLockout;
  pin_.setpoint  = S.setpoint;
  pin_.flowValid = S.useFlow;
  pin_.ovrActive = gOvr;
  pin_.ovrHz     = gOvrPct * 0.6f;        // 100 % = 60 Hz
  // One rule for both paths: a lag pump is available when it is fitted, in
  // comms, and not tripped.  Simulation used to skip the trip test, so an
  // injected fault on the lag pump was invisible to staging.
  pin_.lagAvail  = drv[1].present && drv[1].commsOK && !drv[1].tripped;
  bool reset = (gCmdReset && now - gCmdReset < 1000);

if (S.simOn) {
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
    // An injected trip on the lead drive takes the transducer with it, so the
    // loop sees invalid pressure and freezes -- the real failure, simulated.
    pin_.psiValid  = (drv[0].simTrip == 0);
    pin_.flowGPM   = plant.flowGPM;
    pc.step(pin_, pout, h);
    plant.step(pout.hzCmd, (pout.runLead ? 1 : 0) + (pout.runLag ? 1 : 0), h);
    // Pressure-hold mode: the header is an infinitely stiff source at whatever
    // the operator dialled in, so whatever the pump just did to it is discarded.
    if (S.simMode == 1) plant.psi = S.simPsi;
  }
  // ---- synthesise what the drives would be REPORTING ---------------------
  // Injected trips are cleared by the /cmd reset handler, not here: see the
  // comment there for why the 1 s pulse window was the wrong mechanism.

  for (int i = 0; i < MAX_DRIVES; i++) {
    Drive &d = drv[i];
    bool want = (i == 0) ? pout.runLead : pout.runLag;
    bool fitted = (i < S.simDrives);
    // A tripped drive stops turning whatever the controller asks of it.
    bool spinning = want && fitted && !d.simTrip;

    d.hz   = spinning ? plant.hzAct : 0;
    d.amps = spinning ? plant.amps  : 0;

    // Heatsink: first-order rise toward an ambient-plus-load equilibrium, so
    // it behaves like a thermal mass instead of snapping between two numbers.
    float tgt = 28.0f + (spinning ? 0.55f * d.hz : 0.0f);
    d.tempC += (tgt - d.tempC) * fminf(1.0f, dtc / 90.0f);

    // The transducer is on the lead drive's AI1, so only that one reads it.
    d.aiCounts = (i == 0 && S.xdcrSpanPsi > 0)
               ? (uint16_t)limitf(0, plant.psi / S.xdcrSpanPsi * 1000.0f, 1000)
               : 0;

    // Build the word, then decode it through the SAME function the Modbus
    // path uses.  running/tripped/tripCode are never set directly here.
    d.status = buildStatus(spinning, d.simTrip, fitted);
    decodeStatus(d);

    d.commsOK = fitted;
    d.writeOK = fitted;               // nothing is written while simulating
    if (!fitted) { d.tempC = 0; d.aiCounts = 0; }
  }

  gPsiRaw = plant.psi;
  // A tripped lead drive takes the transducer with it -- the loop must freeze
  // and stop the pumps, exactly as it does when a real one trips.  This is the
  // whole point of being able to inject a fault.
  gPsiValid = !drv[0].tripped && drv[0].commsOK;
} else {
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
}

  // ---- 4. CSV for the bench
  // The per-drive tail is the raw Modbus evidence: what came back from regs
  // 6/7/8 (status, Hz, A) and whether the last read and the last command write
  // were acknowledged.  On the bench this is the whole point of the log.
  Serial.printf("%lu,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%s,%d,%d,%d,"
                "%.1f,%.1f,0x%04X,%d,%d,%.1f,%.1f,0x%04X,%d,%d\n",
    now, pin_.psi, pout.spActive, pout.hzCmd, pout.capHz, pout.shutoffHz,
    plant.flowGPM, stateName(pout.state), pout.runLead, pout.runLag, pc.sleepStage,
    drv[0].hz, drv[0].amps, drv[0].status, drv[0].commsOK, drv[0].writeOK,
    drv[1].hz, drv[1].amps, drv[1].status, drv[1].commsOK, drv[1].writeOK);

  // ---- 5. hand a snapshot to the net task.  Copy only; it publishes on its
  //         own schedule and on its own core, so nothing here can block.
  NetTelem tl;
  tl.psi     = pin_.psi;      tl.spAct   = pout.spActive;
  tl.hzCmd   = pout.hzCmd;    tl.cap     = pout.capHz;
  tl.shutoff = pout.shutoffHz;
  // Was plant.flowGPM -- the SIMULATOR's flow, published unconditionally.  With
  // sim off, plant.step() never runs, so that number was 0 at boot or frozen at
  // whatever the last simulated value happened to be, and it went into the
  // historian looking exactly like a measurement.  flowAt() is the affinity-law
  // curve solved backwards from real pressure and speed: an estimate, but an
  // honest one, and it works on a real pump.
  tl.flow    = flowTotal();
  tl.state   = (int)pout.state; tl.sleepStage = pc.sleepStage;
  tl.enable  = pin_.enable;   tl.runLead = pout.runLead;
  tl.runLag  = pout.runLag;   tl.commsOK = drv[0].commsOK;
  tl.psiValid = gPsiValid;    tl.upSec   = now / 1000;
  tl.a1 = drv[0].amps;        tl.a2 = drv[1].amps;
  tl.t1 = drv[0].tempC;       tl.t2 = drv[1].tempC;
  tl.ai1 = drv[0].aiCounts;
  tl.st1 = drv[0].status;     tl.st2 = drv[1].status;
  tl.trip1 = drv[0].tripCode; tl.trip2 = drv[1].tripCode;
  tl.cycles = pc.sleepCycles;   // rssi is filled by the net task: WiFi calls
                                // do not belong in the control tick.
  strncpy(tl.stName, stateName(pout.state), sizeof(tl.stName) - 1);
  tl.stName[sizeof(tl.stName) - 1] = 0;
  netPushTelem(tl);
}
