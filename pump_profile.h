#pragma once

/*
  Pump profiles -- the parameters that describe THE PUMP, separated from the
  ones that describe the site it is installed on.

  The distinction is the whole point.  A cap table, a shutoff head and a motor
  nameplate belong to a "Price 5 HP 460 V" and travel with it to the next
  skid.  A setpoint, a sleep delay and a staging hysteresis belong to the
  installation and must NOT travel -- copying one site's setpoint onto another
  site's pump is how you commission a system to the wrong pressure.

  So this is a strict subset of Settings, and applying a profile deliberately
  leaves setpoint, sleep and staging alone.

  Three sources feed one dropdown:
    - built-ins, compiled in below, which are starting points and nothing more
    - user slots in NVS, captured from whatever is currently dialled in
    - a pasted / imported JSON profile

  A built-in is a STARTING POINT, never a substitute for measuring.  Every
  field here is per-installation in reality -- impeller trim, suction
  conditions and pipe losses all move the numbers.  The UI says so, because a
  template that looks authoritative is worse than no template at all.
*/

String jstr(const char *s);   // .ino -- JSON string escaper

#define PROFILE_SLOTS   4          // user-saved slots in NVS
#define PROFILE_MAGIC   0x50505231 // 'PPR1'

struct PumpProfile {
  char  name[28] = "";

  // ---- motor nameplate.  Not used by the control block; they exist so a
  // profile identifies a real machine, and so measured amps can be checked
  // against the FLA the motor is actually rated for.
  float hp      = 0;
  float volts   = 0;
  float fla     = 0;              // full load amps, nameplate
  float rpm     = 0;

  // ---- hydraulic model.  shutoffPsiAt60 is the only one the envelope and
  // the sim genuinely need; the other two scale the flow axis for display.
  float shutoffPsiAt60 = 84.0f;
  float qMax60         = 142.0f;
  float cavOnsetGPM    = 103.0f;

  // ---- speed limits
  float minHz = 30.0f;
  float maxHz = 60.0f;

  // ---- cavitation cap table
  int   capPts    = 3;
  float capPsi[4] = {0.0f, 26.0f, 44.0f, 44.0f};
  float capHzPt[4]= {40.0f, 50.0f, 60.0f, 60.0f};
};

/* ---------------------------------------------------------------- built-ins
   Starting points only.  The bench pump is the one set of numbers here that
   was actually measured (DESIGN_NOTES: 84 psi shutoff, ~98 gpm at the cap
   curve's worst point); the rest are plausible shapes for their class and are
   labelled so nobody mistakes them for measurements.  Add rows as real pumps
   get profiled -- that is the point of the export on the Pump screen.        */
static const PumpProfile PROFILE_BUILTIN[] = {
  // name                         hp   V     FLA   rpm    shut  qMax  cav   min max pts  capPsi                capHz
  { "Bench pump (measured)",      5.0f, 460.f, 6.5f, 3450.f, 84.f, 142.f, 103.f, 30.f, 60.f, 3, {0,26,44,44}, {40,50,60,60} },
  { "Price 5 HP 460 V (draft)",   5.0f, 460.f, 6.5f, 3450.f, 84.f, 142.f, 103.f, 30.f, 60.f, 3, {0,26,44,44}, {40,50,60,60} },
  { "Ebara 3 HP 240 V (draft)",   3.0f, 240.f, 9.6f, 3450.f, 62.f, 105.f,  78.f, 30.f, 60.f, 3, {0,20,34,34}, {40,50,60,60} },
};
#define PROFILE_BUILTIN_N ((int)(sizeof(PROFILE_BUILTIN)/sizeof(PROFILE_BUILTIN[0])))

// ---------------------------------------------------------------- user slots
struct ProfileStore {
  uint32_t    magic = PROFILE_MAGIC;
  bool        used[PROFILE_SLOTS] = {false, false, false, false};
  PumpProfile slot[PROFILE_SLOTS];
};

static ProfileStore gProf;
static Preferences  gProfPrefs;

void profLoad() {
  ProfileStore d;
  gProfPrefs.begin("pumpprof", true);
  if (gProfPrefs.isKey("blob")) gProfPrefs.getBytes("blob", &gProf, sizeof(gProf));
  gProfPrefs.end();
  if (gProf.magic != PROFILE_MAGIC) gProf = d;
}

void profSave() {
  gProfPrefs.begin("pumpprof", false);
  gProfPrefs.putBytes("blob", &gProf, sizeof(gProf));
  gProfPrefs.end();
}

// ---------------------------------------------------------------- capture / apply
// Pull the pump-shaped fields OUT of the live settings.
PumpProfile profCapture(const Settings &s, const PumpCfg &c, const char *name) {
  PumpProfile p;
  strncpy(p.name, name, sizeof(p.name) - 1); p.name[sizeof(p.name) - 1] = 0;
  p.hp    = s.motorHp;   p.volts = s.motorVolts;
  p.fla   = s.motorFLA;  p.rpm   = s.motorRPM;
  p.shutoffPsiAt60 = c.shutoffPsiAt60;
  p.qMax60         = s.qMax60;
  p.cavOnsetGPM    = s.cavOnsetGPM;
  p.minHz = c.minHz;  p.maxHz = c.maxHz;
  p.capPts = c.capPts;
  for (int i = 0; i < 4; i++) { p.capPsi[i] = c.capPsi[i]; p.capHzPt[i] = c.capHzPt[i]; }
  return p;
}

// Push a profile INTO the live settings.  Setpoint, sleep and staging are
// deliberately untouched -- see the header comment.
void profApply(const PumpProfile &p, Settings &s, PumpCfg &c) {
  strncpy(s.pumpName, p.name, sizeof(s.pumpName) - 1);
  s.pumpName[sizeof(s.pumpName) - 1] = 0;
  s.motorHp = p.hp;  s.motorVolts = p.volts;
  s.motorFLA = p.fla; s.motorRPM = p.rpm;
  s.qMax60      = p.qMax60;
  s.cavOnsetGPM = p.cavOnsetGPM;
  c.shutoffPsiAt60 = p.shutoffPsiAt60;
  c.minHz = p.minHz;  c.maxHz = p.maxHz;
  c.capPts = p.capPts;
  for (int i = 0; i < 4; i++) { c.capPsi[i] = p.capPsi[i]; c.capHzPt[i] = p.capHzPt[i]; }
}

// ---------------------------------------------------------------- json
static String profJSON(const PumpProfile &p) {
  String j = "{\"name\":" + jstr(p.name);
  j += ",\"hp\":"    + String(p.hp, 1);
  j += ",\"volts\":" + String(p.volts, 0);
  j += ",\"fla\":"   + String(p.fla, 1);
  j += ",\"rpm\":"   + String(p.rpm, 0);
  j += ",\"shutoffPsiAt60\":" + String(p.shutoffPsiAt60, 1);
  j += ",\"qMax60\":"      + String(p.qMax60, 0);
  j += ",\"cavOnsetGPM\":" + String(p.cavOnsetGPM, 0);
  j += ",\"minHz\":" + String(p.minHz, 1) + ",\"maxHz\":" + String(p.maxHz, 1);
  j += ",\"capPts\":" + String(p.capPts) + ",\"capPsi\":[";
  for (int i = 0; i < 4; i++) j += String(p.capPsi[i], 0)  + (i < 3 ? "," : "");
  j += "],\"capHz\":[";
  for (int i = 0; i < 4; i++) j += String(p.capHzPt[i], 1) + (i < 3 ? "," : "");
  j += "]}";
  return j;
}

// Missing keys keep the default, so a hand-trimmed file still imports -- the
// same contract as config_io.h.
static PumpProfile profParse(const String &b) {
  PumpProfile p;
  cfgS(b, "name", p.name, sizeof(p.name));
  p.hp    = cfgF(b, "hp",    p.hp);
  p.volts = cfgF(b, "volts", p.volts);
  p.fla   = cfgF(b, "fla",   p.fla);
  p.rpm   = cfgF(b, "rpm",   p.rpm);
  p.shutoffPsiAt60 = limitf(20, cfgF(b, "shutoffPsiAt60", p.shutoffPsiAt60), 300);
  p.qMax60         = limitf(1,  cfgF(b, "qMax60",      p.qMax60), 5000);
  p.cavOnsetGPM    = limitf(1,  cfgF(b, "cavOnsetGPM", p.cavOnsetGPM), 5000);
  p.minHz = limitf(10, cfgF(b, "minHz", p.minHz), 55);
  p.maxHz = limitf(20, cfgF(b, "maxHz", p.maxHz), 60);
  p.capPts = (int)limitf(2, cfgF(b, "capPts", p.capPts), 4);
  cfgArr(b, "capPsi", p.capPsi,  4);
  cfgArr(b, "capHz",  p.capHzPt, 4);
  for (int i = 0; i < 4; i++) {
    p.capPsi[i]  = limitf(0, p.capPsi[i],  300);
    p.capHzPt[i] = limitf(0, p.capHzPt[i], 60);
  }
  return p;
}

// ---------------------------------------------------------------- listing
// One flat list the dropdown can render: built-ins first, then user slots.
// Ids are "b0.." and "u0.." rather than a bare index, so inserting a built-in
// later cannot silently repoint a saved reference at a different pump.
String profListJSON() {
  String j = "{\"builtin\":[";
  for (int i = 0; i < PROFILE_BUILTIN_N; i++) {
    if (i) j += ",";
    j += "{\"id\":\"b" + String(i) + "\",\"name\":" + jstr(PROFILE_BUILTIN[i].name) + "}";
  }
  j += "],\"user\":[";
  bool first = true;
  for (int i = 0; i < PROFILE_SLOTS; i++) {
    if (!gProf.used[i]) continue;
    if (!first) j += ",";
    first = false;
    j += "{\"id\":\"u" + String(i) + "\",\"slot\":" + String(i) +
         ",\"name\":" + jstr(gProf.slot[i].name) + "}";
  }
  j += "],\"slots\":" + String(PROFILE_SLOTS) + "}";
  return j;
}

// Resolve "b1" / "u2" to a profile.  Returns false if the id names nothing.
bool profById(const String &id, PumpProfile &out) {
  if (id.length() < 2) return false;
  int n = id.substring(1).toInt();
  if (id[0] == 'b') {
    if (n < 0 || n >= PROFILE_BUILTIN_N) return false;
    out = PROFILE_BUILTIN[n];
    return true;
  }
  if (id[0] == 'u') {
    if (n < 0 || n >= PROFILE_SLOTS || !gProf.used[n]) return false;
    out = gProf.slot[n];
    return true;
  }
  return false;
}
