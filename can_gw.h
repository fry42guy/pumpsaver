#pragma once

/*
  CANopen slave that impersonates an Optidrive E3 to the CR1082 PLC.

  The AIO skid's CODESYS code cannot change. It talks CANopen to node 1 and
  believes node 1 is a drive. So this board becomes node 1: it answers the
  PLC on CAN and drives the real E3 over Modbus RS485. The PLC's POU, its
  CANopen Manager, and its HMI all keep working untouched.

        CR1082  --CAN 250k-->  [ this board ]  --Modbus RTU-->  E3
                 node 1                          addr 2..5

  Four rules shape this file.

  1. TRANSLATE AS LITTLE AS POSSIBLE. The E3's Modbus and CAN interfaces
     carry the same words in the same units: control word (9.2.1), speed
     reference x10 (9.2.2), status + trip code (9.2.4 -- a section headed
     "CAN or Modbus or Both"). Status, speed and current are therefore
     copied register-to-PDO verbatim, not decoded and re-encoded. Every
     translation is a chance to be subtly wrong, and a value the PLC
     misreads by a factor of ten is a pump at the wrong speed.

  2. PARAMETERS ARE SHADOWED, NOT FORWARDED. The PLC writes P-01 as the
     cavitation cap, P-02 as the floor. It does NOT need those to land in
     the drive: this board clamps the speed reference itself before it ever
     reaches the drive, which enforces the same limit one link earlier and
     sidesteps the E3's undocumented Modbus parameter scaling ("internal
     scaling is used on some parameters, contact your Sales Partner").
     Writes are stored and echoed back on read, so the POU's P-01/P-02
     read-back check passes and the limits are genuinely enforced -- just
     by this board rather than by the drive. Anything the drive really must
     know (ramps, motor current) is forwarded and marked FWD below.

  3. THE OBJECT DICTIONARY AGREES WITH THE MASTER. The ifm CANopen Manager
     runs a configuration sequence before the POU sees anything: it reads
     identity, writes PDO communication and mapping parameters, sets the
     heartbeat. We do not have the EDS it is working from, so rather than
     guess, standard objects (0x1000-0x1FFF) are stored on write and
     returned on read. The node agrees with whatever the Manager asks for,
     which is what a real slave configured from that same EDS would do.
     Identity (0x1018) is the exception -- if the Manager has identity
     checking enabled it compares against the EDS, so those values are
     configurable and should be CAPTURED FROM THE REAL DRIVE with listen
     mode below rather than invented.

  4. A DEAD DRIVE LOOKS DEAD. If Modbus stops answering, this file stops
     transmitting: no TX PDO1, no heartbeat. The POU's own 1 s comms
     watchdog and 10 s CANopenState timer both trip and the existing fault
     logic handles it. Publishing a cheerful synthesized status over a
     drive that is not there would be the one lie that matters.

  LISTEN MODE. Set listenOnly and this becomes a passive logger: it ACKs
  nothing, transmits nothing, and records every frame on the bus into a
  ring buffer readable at /can. Put it on the working PLC+drive bus, power
  cycle the PLC, and you have the Manager's real startup sequence and the
  drive's real answers -- including the identity values above.
*/

#include "driver/twai.h"
#include <Preferences.h>

#define CAN_TX_PIN 15
#define CAN_RX_PIN 16

// ---------------------------------------------------------------- config
struct CanCfg {
  uint32_t magic      = 0x50534347;   // 'PSCG'
  bool     enable     = false;        // gateway off until commissioned
  bool     listenOnly = false;        // passive logger, transmits nothing
  uint8_t  node       = 1;            // POU is hard-wired to node 1
  uint16_t kbit       = 250;          // can0_en(eBaudrate := 250)
  uint16_t hbMs       = 500;          // POU trips at 3 s with no 0x701
  uint16_t pdoMs      = 100;          // POU trips at 1 s with no 0x181
  uint8_t  driveAddr  = 0;            // 0 = use the first discovered drive
  bool     capEnforce = true;         // clamp the PLC's reference to our cap
  // Identity, 0x1018. Zeros mean "not captured yet" -- see listen mode.
  uint32_t devType = 0, vendor = 0, product = 0, revision = 0, serial = 0;
};

// ---------------------------------------------------------------- trace
struct CanTrace { uint32_t ms; uint32_t id; uint8_t dlc, dir, d[8]; };
#define CAN_TRACE_N 160
static CanTrace  gTr[CAN_TRACE_N];
static volatile uint16_t gTrHead = 0;
static volatile uint32_t gTrCount = 0;
static portMUX_TYPE gTrMux = portMUX_INITIALIZER_UNLOCKED;

static void trLog(uint32_t id, const uint8_t *d, uint8_t dlc, uint8_t dir) {
  portENTER_CRITICAL(&gTrMux);
  CanTrace &t = gTr[gTrHead];
  t.ms = millis(); t.id = id; t.dlc = dlc; t.dir = dir;
  for (int i = 0; i < 8; i++) t.d[i] = i < dlc ? d[i] : 0;
  gTrHead = (gTrHead + 1) % CAN_TRACE_N;
  gTrCount++;
  portEXIT_CRITICAL(&gTrMux);
}

// ---------------------------------------------------------------- state
static CanCfg     gCan;
static Preferences gCanPrefs;

enum NmtState : uint8_t { NMT_BOOT = 0, NMT_STOPPED = 4, NMT_OP = 5, NMT_PREOP = 127 };
static volatile uint8_t gNmt = NMT_BOOT;

static bool     gCanUp        = false;   // driver installed
static uint32_t gRxPdoMs      = 0;       // last 0x201 from the PLC
static uint32_t gSdoCount     = 0, gSdoAbort = 0;
static uint16_t gCtrlWord     = 0;       // as sent by the PLC
static float    gRefHz        = 0;       // as sent by the PLC
static float    gAppliedHz    = 0;       // after our clamp
static const char *gClampWhy  = "none";

// Shadow parameters, in the CAN units the POU uses. Index is the P number.
static uint16_t gPar[61];
static bool     gParSet[61];

// Generic store for standard objects the Manager configures.
struct OdEnt { uint16_t idx; uint8_t sub; uint32_t val; };
static OdEnt   gOd[56];
static uint8_t gOdN = 0;

// ---------------------------------------------------------------- nvs
void canLoad() {
  CanCfg d;
  gCanPrefs.begin("pscan", true);
  if (gCanPrefs.isKey("blob")) gCanPrefs.getBytes("blob", &gCan, sizeof(gCan));
  gCanPrefs.end();
  if (gCan.magic != d.magic) gCan = d;
}
static void canSave() {
  gCanPrefs.begin("pscan", false);
  gCanPrefs.putBytes("blob", &gCan, sizeof(gCan));
  gCanPrefs.end();
}

// ---------------------------------------------------------------- helpers
static bool canSend(uint32_t id, const uint8_t *d, uint8_t dlc) {
  if (gCan.listenOnly) return false;
  twai_message_t m = {};
  m.identifier = id; m.data_length_code = dlc;
  for (int i = 0; i < dlc; i++) m.data[i] = d[i];
  bool ok = twai_transmit(&m, pdMS_TO_TICKS(5)) == ESP_OK;
  if (ok) trLog(id, d, dlc, 1);
  return ok;
}

// The E3's manufacturer objects carry 16-bit values BIG-endian over SDO --
// the POU byte-swaps every one of them by hand on the way in and out
// (uiParamP01 := (counts MOD 256)*256 + (counts/256)). CANopen itself is
// little-endian; this is the drive's own quirk and we have to match it.
static inline uint16_t bswap16(uint16_t v) { return (uint16_t)((v >> 8) | (v << 8)); }

static int odFind(uint16_t idx, uint8_t sub) {
  for (int i = 0; i < gOdN; i++) if (gOd[i].idx == idx && gOd[i].sub == sub) return i;
  return -1;
}
static void odSet(uint16_t idx, uint8_t sub, uint32_t v) {
  int i = odFind(idx, sub);
  if (i >= 0) { gOd[i].val = v; return; }
  if (gOdN < (int)(sizeof(gOd) / sizeof(gOd[0]))) gOd[gOdN++] = { idx, sub, v };
}

// ---------------------------------------------------------------- drive link
// Which discovered drive we are fronting for.
static Drive *gwDrive() {
  if (gCan.driveAddr) {
    for (int i = 0; i < MAX_DRIVES; i++)
      if (drv[i].present && drv[i].addr == gCan.driveAddr) return &drv[i];
    return nullptr;
  }
  for (int i = 0; i < MAX_DRIVES; i++) if (drv[i].present) return &drv[i];
  return nullptr;
}
static bool gwLinkOK() { Drive *d = gwDrive(); return d && d->commsOK; }

// ---------------------------------------------------------------- SDO server
/*
  Expedited transfers only. Every object the POU touches is 16-bit, and the
  Manager's configuration writes are 8/16/32-bit expedited, so segmented
  transfer would be dead code. A segmented request is aborted cleanly rather
  than answered wrongly.

  Abort is used sparingly. A master that gets an abort on a configuration
  write may mark the node unconfigured and never start it, so unknown
  objects read back as zero rather than aborting -- the same thing a real
  slave does for an object it implements but has never had written.
*/
#define SDO_ABORT_NOSUB   0x06090011UL   // sub-index does not exist
#define SDO_ABORT_SEG     0x05040001UL   // command specifier not valid

static int sdoAbort(uint8_t *rsp, uint32_t code) {
  rsp[0] = 0x80;
  for (int i = 0; i < 4; i++) rsp[4 + i] = (uint8_t)(code >> (8 * i));
  gSdoAbort++;
  return 8;
}

static uint16_t sdoReadParam(uint8_t p);      // fwd -- needs the Modbus layer

// Returns bytes to send in rsp (always 8), or 0 for "say nothing".
static int sdoHandle(const uint8_t *req, uint8_t *rsp) {
  uint8_t  cmd = req[0];
  uint16_t idx = (uint16_t)req[1] | ((uint16_t)req[2] << 8);
  uint8_t  sub = req[3];
  uint8_t  ccs = cmd & 0xE0;

  rsp[1] = req[1]; rsp[2] = req[2]; rsp[3] = sub;
  for (int i = 4; i < 8; i++) rsp[i] = 0;

  // ---------------- upload (master reads us) ----------------
  if (ccs == 0x40) {
    uint32_t v = 0; uint8_t n = 0;

    if (idx == 0x2013) {                       // AI1 percent x10, live
      v = bswap16(sdoReadParam(0xFF)); n = 2;
    } else if (idx >= 0x2064 && idx <= 0x20A0) {   // P-00 .. P-60 shadow
      uint8_t p = (uint8_t)(idx - 0x2064);
      v = bswap16(p <= 60 ? gPar[p] : 0); n = 2;
    } else if (idx >= 0x2000 && idx < 0x2064) {    // other manufacturer area
      int i = odFind(idx, sub);
      v = i >= 0 ? gOd[i].val : 0; n = 2;
    } else if (idx == 0x1018) {                    // identity
      switch (sub) {
        case 0: v = 4;              n = 1; break;
        case 1: v = gCan.vendor;    n = 4; break;
        case 2: v = gCan.product;   n = 4; break;
        case 3: v = gCan.revision;  n = 4; break;
        case 4: v = gCan.serial;    n = 4; break;
        default: return sdoAbort(rsp, SDO_ABORT_NOSUB);
      }
    } else if (idx == 0x1000) {
      v = gCan.devType; n = 4;
    } else if (idx == 0x1001) {                    // error register
      v = gwLinkOK() ? 0 : 1; n = 1;
    } else {                                       // anything else: stored or 0
      int i = odFind(idx, sub);
      if (i >= 0) { v = gOd[i].val; n = 4; }
      else        { v = 0;          n = 4; }
    }

    rsp[0] = (uint8_t)(0x43 | ((4 - n) << 2));     // expedited, size indicated
    for (int i = 0; i < n; i++) rsp[4 + i] = (uint8_t)(v >> (8 * i));
    gSdoCount++;
    return 8;
  }

  // ---------------- download (master writes us) ----------------
  if (ccs == 0x20) {
    if (!(cmd & 0x02)) return sdoAbort(rsp, SDO_ABORT_SEG);   // segmented
    uint8_t n = (cmd & 0x01) ? (uint8_t)(4 - ((cmd >> 2) & 0x03)) : 4;
    uint32_t v = 0;
    for (int i = 0; i < n; i++) v |= (uint32_t)req[4 + i] << (8 * i);

    if (idx >= 0x2064 && idx <= 0x20A0) {
      uint8_t p = (uint8_t)(idx - 0x2064);
      if (p <= 60) { gPar[p] = bswap16((uint16_t)v); gParSet[p] = true; }
    } else {
      odSet(idx, sub, v);
      if (idx == 0x1017 && v >= 50 && v <= 3000) gCan.hbMs = (uint16_t)v;
    }

    rsp[0] = 0x60;                                  // download confirm
    gSdoCount++;
    return 8;
  }

  return sdoAbort(rsp, SDO_ABORT_SEG);
}

// ---------------------------------------------------------------- shadow params
// P-01 and P-02 arrive as 60 counts per Hz -- the POU's own encode is
// TO_DINT(rP01Target * 60.0), byte-swapped. Stored native by sdoHandle().
static float parHz(uint8_t p, float dflt) {
  if (p > 60 || !gParSet[p] || gPar[p] == 0) return dflt;
  return (float)gPar[p] / 60.0f;
}
static float gwMaxHz() { return parHz(1, 60.0f); }   // P-01, the cavitation cap
static float gwMinHz() { return parHz(2,  0.0f); }   // P-02, the floor

// ---------------------------------------------------------------- PDO
// TX PDO1 is a verbatim copy of four Modbus registers. Nothing is decoded
// and re-encoded, because the E3 carries the same words in the same units
// on both links (user guide 9.2, "CAN or Modbus or Both").
static uint16_t gTempC = 0;

static void sendTxPdo1() {
  Drive *d = gwDrive();
  if (!d || !d->commsOK) return;                  // rule 4: a dead drive is silent

  uint16_t w0 = d->status;                        // reg 6  status + trip code
  uint16_t w1 = (uint16_t)lroundf(d->hz   * 10);  // reg 7  Hz x10
  uint16_t w2 = (uint16_t)lroundf(d->amps * 10);  // reg 8  A  x10
  uint16_t w3 = gTempC;                           // reg 24 heatsink degC

  uint8_t p[8] = { (uint8_t)w0, (uint8_t)(w0 >> 8), (uint8_t)w1, (uint8_t)(w1 >> 8),
                   (uint8_t)w2, (uint8_t)(w2 >> 8), (uint8_t)w3, (uint8_t)(w3 >> 8) };
  canSend(0x180 + gCan.node, p, 8);
}

// RX PDO1 from the PLC: control word, speed reference x10, ramp.
static void onRxPdo1(const uint8_t *d, uint8_t dlc) {
  if (dlc < 4) return;
  gRxPdoMs  = millis();
  gCtrlWord = (uint16_t)d[0] | ((uint16_t)d[1] << 8);
  gRefHz    = ((uint16_t)d[2] | ((uint16_t)d[3] << 8)) / 10.0f;

  float hz = gRefHz;
  gClampWhy = "none";
  if (gCan.capEnforce) {
    float mx = gwMaxHz(), mn = gwMinHz();
    if (hz > mx) { hz = mx; gClampWhy = "P-01 cap"; }
    // The floor only applies while the PLC is actually asking for run. A
    // stop must reach zero, or the pump can never be commanded off.
    if ((gCtrlWord & 0x0001) && hz < mn && hz > 0) { hz = mn; gClampWhy = "P-02 floor"; }
  }
  if (hz < 0) hz = 0;
  if (hz > 60) hz = 60;
  gAppliedHz = hz;

  Drive *drvp = gwDrive();
  if (!drvp) return;
  // Control word bits pass through unchanged: bit0 run, bit1 fast stop,
  // bit2 reset, bit3 coast are identical on CAN and Modbus (9.2.1).
  uint16_t w[2] = { gCtrlWord, (uint16_t)lroundf(hz * 10) };
  rtu.writeMultiple(drvp->addr, REG(1), w, 2);
}

// ---------------------------------------------------------------- NMT
static void sendHeartbeat() {
  uint8_t s = gNmt;
  canSend(0x700 + gCan.node, &s, 1);
}

static void onNmt(const uint8_t *d, uint8_t dlc) {
  if (dlc < 2) return;
  if (d[1] != gCan.node && d[1] != 0) return;
  switch (d[0]) {
    case 0x01: gNmt = NMT_OP;      break;         // start
    case 0x02: gNmt = NMT_STOPPED; break;
    case 0x80: gNmt = NMT_PREOP;   break;
    case 0x81:                                     // reset node
    case 0x82:                                     // reset communication
      gNmt = NMT_BOOT;
      { uint8_t z = 0x00; canSend(0x700 + gCan.node, &z, 1); }   // boot-up
      gNmt = NMT_PREOP;
      break;
  }
}

// ---------------------------------------------------------------- task
static void canTask(void *) {
  uint32_t nextHb = 0, nextPdo = 0, nextTemp = 0;

  // Boot-up message, then PRE-OPERATIONAL, exactly as a real slave does.
  if (!gCan.listenOnly) {
    uint8_t z = 0x00;
    canSend(0x700 + gCan.node, &z, 1);
    gNmt = NMT_PREOP;
  }

  for (;;) {
    twai_message_t m;
    while (twai_receive(&m, 0) == ESP_OK) {
      if (m.extd || m.rtr) continue;
      trLog(m.identifier, m.data, m.data_length_code, 0);
      if (gCan.listenOnly) continue;

      uint32_t id = m.identifier;
      if (id == 0x000) {
        onNmt(m.data, m.data_length_code);
      } else if (id == 0x600u + gCan.node) {
        uint8_t rsp[8];
        if (sdoHandle(m.data, rsp) == 8) canSend(0x580 + gCan.node, rsp, 8);
      } else if (id == 0x200u + gCan.node) {
        if (gNmt == NMT_OP) onRxPdo1(m.data, m.data_length_code);
      }
    }

    uint32_t now = millis();
    if (!gCan.listenOnly && gNmt != NMT_STOPPED) {
      if (now >= nextHb)  { nextHb  = now + gCan.hbMs;  if (gwLinkOK()) sendHeartbeat(); }
      if (now >= nextPdo && gNmt == NMT_OP) { nextPdo = now + gCan.pdoMs; sendTxPdo1(); }
    }
    if (now >= nextTemp) {
      nextTemp = now + 2000;
      Drive *d = gwDrive();
      uint16_t t;
      if (d && d->present && rtu.readHolding(d->addr, REG(24), 1, &t) && t <= 150) gTempC = t;
    }

    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

// ---------------------------------------------------------------- api
bool gwOwnsDrive() { return gCan.enable && !gCan.listenOnly && gNmt == NMT_OP; }

void canBegin() {
  canLoad();
  if (!gCan.enable) return;

  // The POU only closes its own PI loop when it reads P-12 = 7. Behind this
  // gateway the real drive is in Modbus mode and has no internal PI to hand
  // the loop to, so legacy mode 5 would leave nobody controlling pressure.
  // Default the shadow to 7; a later write from the PLC still wins.
  gPar[12] = 7; gParSet[12] = true;

  twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT(
      (gpio_num_t)CAN_TX_PIN, (gpio_num_t)CAN_RX_PIN,
      gCan.listenOnly ? TWAI_MODE_LISTEN_ONLY : TWAI_MODE_NORMAL);
  g.rx_queue_len = 32; g.tx_queue_len = 16;
  twai_timing_config_t t = TWAI_TIMING_CONFIG_250KBITS();
  twai_filter_config_t f = TWAI_FILTER_CONFIG_ACCEPT_ALL();

  if (twai_driver_install(&g, &t, &f) != ESP_OK) return;
  if (twai_start() != ESP_OK) { twai_driver_uninstall(); return; }
  gCanUp = true;
  xTaskCreatePinnedToCore(canTask, "can", 4096, nullptr, 2, nullptr, 0);
}

void canStage(const CanCfg &c) { gCan = c; gCan.magic = CanCfg().magic; canSave(); }
CanCfg canCurrent() { return gCan; }

// ---------------------------------------------------------------- json
String canStatusJSON() {
  const char *st = gNmt == NMT_OP ? "operational" : gNmt == NMT_PREOP ? "pre-op"
                 : gNmt == NMT_STOPPED ? "stopped" : "boot";
  String j = "{";
  j += "\"up\":"        + String(gCanUp ? "true" : "false");
  j += ",\"enable\":"   + String(gCan.enable ? "true" : "false");
  j += ",\"listen\":"   + String(gCan.listenOnly ? "true" : "false");
  j += ",\"node\":"     + String(gCan.node);
  j += ",\"nmt\":\""    + String(st) + "\"";
  j += ",\"frames\":"   + String(gTrCount);
  j += ",\"sdo\":"      + String(gSdoCount);
  j += ",\"aborts\":"   + String(gSdoAbort);
  j += ",\"rxAgeMs\":"  + String(gRxPdoMs ? millis() - gRxPdoMs : 0);
  j += ",\"ctrl\":"     + String(gCtrlWord);
  j += ",\"refHz\":"    + String(gRefHz, 1);
  j += ",\"appliedHz\":"+ String(gAppliedHz, 1);
  j += ",\"clamp\":\""  + String(gClampWhy) + "\"";
  j += ",\"p01Hz\":"    + String(gwMaxHz(), 1);
  j += ",\"p02Hz\":"    + String(gwMinHz(), 1);
  j += ",\"p12\":"      + String(gPar[12]);
  j += ",\"link\":"     + String(gwLinkOK() ? "true" : "false");
  j += ",\"tempC\":"    + String(gTempC);
  j += ",\"vendor\":"   + String(gCan.vendor);
  j += ",\"product\":"  + String(gCan.product);
  /* Every field /canset accepts has to come back out here.  These six did not,
     so the web form rendered them EMPTY, and saving posted "" for each --
     which argF turns into 0.  Pressing "Save gateway" therefore switched OFF
     capEnforce and zeroed driveAddr, revision, serial and devType, while hbMs
     and pdoMs got clamped to their minimums.  A form that cannot show what is
     stored will overwrite it; the fix belongs on this side, not in the page. */
  j += ",\"capEnforce\":" + String(gCan.capEnforce ? "true" : "false");
  j += ",\"driveAddr\":"  + String(gCan.driveAddr);
  j += ",\"hbMs\":"       + String(gCan.hbMs);
  j += ",\"pdoMs\":"      + String(gCan.pdoMs);
  j += ",\"revision\":"   + String(gCan.revision);
  j += ",\"serial\":"     + String(gCan.serial);
  j += ",\"devType\":"    + String(gCan.devType);
  j += "}";
  return j;
}

// Frame log, oldest first. Text, not JSON -- it is read by a human.
String canTraceText() {
  String s = "";
  portENTER_CRITICAL(&gTrMux);
  uint16_t h = gTrHead; uint32_t n = gTrCount < CAN_TRACE_N ? gTrCount : CAN_TRACE_N;
  CanTrace snap[CAN_TRACE_N];
  for (uint32_t i = 0; i < n; i++) snap[i] = gTr[(h + CAN_TRACE_N - n + i) % CAN_TRACE_N];
  portEXIT_CRITICAL(&gTrMux);

  char line[80];
  for (uint32_t i = 0; i < n; i++) {
    CanTrace &t = snap[i];
    int k = snprintf(line, sizeof(line), "%8lu %s %03lX [%u] ",
                     (unsigned long)t.ms, t.dir ? "TX" : "RX",
                     (unsigned long)t.id, t.dlc);
    for (int b = 0; b < t.dlc && k < 72; b++) k += snprintf(line + k, sizeof(line) - k, "%02X ", t.d[b]);
    s += line; s += "\n";
  }
  return s.length() ? s : String("(no frames)\n");
}

// ---------------------------------------------------------------- live reads
// The only object the POU reads that must come off the drive rather than the
// shadow: AI1, which is the pressure transducer. 0..1000 = 0..100.0 % of
// span, and the POU scales it by 0.2 to get psi. Measured, never invented --
// on a Modbus failure the last good value is held and the heartbeat stops,
// so the PLC sees a dead node rather than a stale pressure.
static uint16_t gAi1 = 0;
static uint16_t sdoReadParam(uint8_t p) {
  if (p == 0xFF) {
    Drive *d = gwDrive();
    uint16_t v;
    if (d && d->present && rtu.readHolding(d->addr, REG(20), 1, &v) && v <= 1000) gAi1 = v;
    return gAi1;
  }
  return 0;
}
