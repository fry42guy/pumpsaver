#pragma once
#include <math.h>
#include <stdint.h>

/*
  PumpControl -- faithful C++ port of FB_PumpControl (CODESYS ST).

  Deliberately free of Arduino dependencies: pressure and a setpoint go in,
  a speed and a pump count come out.  That separation is the point -- this
  block compiles on the desktop and can be exercised against a simulated
  plant with no hardware attached, which is how the gains should be tuned.

  Caller owns: transducer validation, drive faults, run permissives, and
  deciding which physical drive is lead this cycle.

  hzCmd goes to EVERY running pump.  Parallel pumps on a common discharge
  header must run at the same speed -- a pump turning slower than its
  partner has a shutoff head below the header pressure, so it delivers
  nothing while it churns.  Staging is a discrete decision (how many pumps)
  layered on one continuous speed.
*/

enum PumpState {
  STATE_IDLE = 0, STATE_FILL = 1, STATE_REGULATE = 2, STATE_CAPPED = 3,
  STATE_CHARGING = 4, STATE_ASLEEP = 5, STATE_STAGED = 6, STATE_FAULT = 7,
  STATE_MANUAL = 8
};

static inline float limitf(float lo, float v, float hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

// TON: Q goes true once IN has been continuously true for PT seconds.
struct Ton {
  float acc = 0; bool q = false;
  void run(bool in, float pt, float dt) {
    if (in) { acc += dt; q = (acc >= pt); }
    else    { acc = 0;   q = false; }
  }
};

struct PumpCfg {
  /* ---- cavitation cap table -----------------------------------------
     Pressure -> highest safe speed.  Bench data: cavitation onset is a
     FLOW limit near 103-107 gpm, near enough independent of speed, so a
     discharge-pressure-indexed speed cap keeps flow under it.  Defaults
     are the bench curve with margin; worst case along it is about 98 gpm.

     Field calibration: hold each speed, open the discharge until it
     rattles, enter (psi_onset + 6) against that Hz.  Both columns must
     increase, and row 1 must sit above minHz, or capTableOK goes false
     and the cap parks at row 1.                                       */
  float capPsi[4]  = {0.0f, 26.0f, 44.0f, 44.0f};
  float capHzPt[4] = {40.0f, 50.0f, 60.0f, 60.0f};
  int   capPts     = 3;
  bool  capEnable  = true;

  float minHz = 30.0f;      // slowest the pump is allowed to turn
  float maxHz = 60.0f;

  float kp         = 0.60f; // Hz per psi
  float ki         = 0.40f; // Hz per psi per second
  float spRampPsiS = 5.0f;  // fill ramp, psi/s
  float spStepPsi  = 20.0f; // fill preload above live pressure

  /* ---- sleep ---------------------------------------------------------
     Phase 1 at the setpoint, then charge +sleepBoost, then phase 2 at the
     raised setpoint.  Phase 2 tests a HIGHER speed threshold, because
     raising the head raises the no-flow speed: 55 -> 60 psi moves shutoff
     from 48.5 to 50.7 Hz.  Both phases hold sleepDlyS, and that hold is
     the only thing limiting cycle rate.

     Speed cannot see a light draw: at 55 psi a 2 gpm draw sits at 48.55 Hz
     against a 48.54 Hz shutoff.  So a trickle WILL duty-cycle.  sleepDlyS
     bounds how often -- 60 s gives about 23 starts an hour on 2 gpm.    */
  /* A FIXED speed threshold breaks as soon as the setpoint moves.  Holding
     70 psi needs 54.8 Hz just to reach shutoff, so with sleepHz = 50 the pump
     can never sleep at any setpoint above about 62 psi -- verified in
     test/harness.cpp.  Measuring the threshold from the shutoff speed instead
     tracks the setpoint automatically, and phase 2 then needs no separate
     offset: the raised charge target raises its own shutoff.

     sleepRelShutoff = false restores the original fixed-threshold behaviour. */
  bool  sleepRelShutoff = true;
  float sleepHzMargin   = 1.5f;  // threshold = shutoff speed + this
  float sleepHz    = 50.0f; // phase 1 threshold when sleepRelShutoff is false
  float sleepHz2   = 3.0f;  // phase 2 offset  when sleepRelShutoff is false
  float sleepDlyS  = 60.0f;
  float chargeRampS = 3.0f; // seconds to ease the setpoint up into the charge
  float sleepBand  = 1.0f;  // pressure satisfied = SP - this
  float sleepBoost = 5.0f;  // charge above setpoint; 0 disables
  float spBoostMax = 90.0f;
  float boostMaxS  = 60.0f; // time allowed to CLIMB to the charge
  float wakeDrop   = 5.0f;  // cut-in: wake at SP - this
  float sleepMinS  = 5.0f;
  float idleGPM    = 2.0f;
  float wakeGPM    = 3.0f;

  /* ---- staging -------------------------------------------------------
     Up when the lead is pinned at its cap and still losing pressure.
     Down when one pump alone could carry the flow: at 55 psi that
     crossover is about 51.6 Hz, so the default sits below it with
     hysteresis.  lagMinRunS stops the lag pump short-cycling.          */
  float stageUpPsi    = 4.0f;
  float stageUpDlyS   = 20.0f;
  float stageDownHz   = 46.0f;
  float stageDownDlyS = 30.0f;
  float lagMinRunS    = 120.0f;

  float shutoffPsiAt60 = 84.0f;  // no-flow head at 60 Hz, for the affinity calc
};

struct PumpIn {
  bool  enable    = false;  // system run demand
  float psi       = 0;      // header pressure, validated by the caller
  bool  psiValid  = false;  // false freezes the loop and stops the pumps
  float setpoint  = 55.0f;  // psi
  float flowGPM   = 0;      // 0 if not metered
  bool  flowValid = false;  // true only when a flow meter is fitted
  bool  lagAvail  = false;  // second pump healthy and available to stage

  /* Diagnostic max-Hz override.  When ovrActive, ovrHz REPLACES the cavitation
     cap and cfg.maxHz as the ceiling, and the minimum-speed floor drops to 0 so
     the pump can be walked all the way down.  This deliberately allows running
     past the cavitation limit -- it is a bench and commissioning tool, not a
     control feature.  It lives in PumpIn, not PumpCfg, precisely so it cannot
     be persisted: like `enable`, it is fed in fresh every tick and a reboot
     always comes back with the cap doing its job.  Sleep is suspended while it
     is active so what you dial in is what the pump runs. */
  bool  ovrActive = false;
  float ovrHz     = 60.0f;

  /* Manual mode.  The commanded speed IS manHz -- no PI, no cavitation cap, no
     sleep, no staging.  Bench and commissioning use: proving a drive, sweeping
     a pump curve, checking rotation.  Like ovrActive it lives here rather than
     in PumpCfg so it cannot be persisted. */
  bool  manMode   = false;
  float manHz     = 0.0f;
};

struct PumpOut {
  float hzCmd     = 0;      // commanded speed, all running pumps
  float capHz     = 40.0f;  // live cavitation cap, for the HMI
  float shutoffHz = 0;      // no-flow speed at this setpoint, reference only
  float spActive  = 0;      // setpoint the loop is chasing (incl. sleep charge)
  bool  runLead   = false;
  bool  runLag    = false;
  int   state     = STATE_IDLE;
};

// Everything the loop is thinking, for the troubleshooting dashboard.  Written
// once per step; never read back by the control logic.
struct PumpDiag {
  float sp = 0, spActive = 0, spEff = 0, err = 0;
  float pTerm = 0, iTerm = 0, raw = 0, clamp = 0;   // clamp = anti-windup correction
  float floorEff = 0, capTarget = 0, psiFilt = 0, psiUse = 0;
  float shutoffSP = 0, shutoffActive = 0, thr1 = 0, thr2 = 0;
  bool  iAtFloor = false, iAtCap = false;           // integrator pinned?
  bool  flowIdle = false, wake = false;
  bool  g1spEff = false, g1psi = false, g1hz = false;   // phase 1 gates
  bool  g2psi = false, g2hz = false;                    // phase 2 gates
  float tP1 = 0, tP2 = 0, tMin = 0, tBst = 0;
  float tUp = 0, tDn = 0, tLag = 0;
  bool  stageUp = false, stageDown = false, lagOn = false;
};

class PumpControl {
public:
  PumpCfg  cfg;
  PumpDiag d;

  // diagnostics
  int      sleepStage   = 0;   // 0 awake, 1 charging, 2 asleep
  uint32_t sleepCycles  = 0;
  uint32_t boostAbandon = 0;
  bool     capTableOK   = false;
  float    spEff        = 0;
  float    pidI         = 0;
  float    psiFilt      = 0;

  void reset() {
    sleepStage = 0; spEff = 0; pidI = 0; hzCmd = 0; lagOn = false;
    init = false; enPrev = false; tCap = 0; tPid = 0;
    tSleepP1 = Ton(); tSleepP2 = Ton(); tSleepMin = Ton(); tBoost = Ton();
    tStageUp = Ton(); tStageDn = Ton(); tLagRun = Ton();
  }

  void step(const PumpIn& in, PumpOut& out, float dt) {
    // ---------------------------------------------------------- 1. SETPOINT
    float sp  = limitf(5.0f, in.setpoint, 100.0f);
    shutoffHz = 60.0f * sqrtf(sp / cfg.shutoffPsiAt60);   // display only

    float spActive = sp;
    if (sleepStage == 1) spActive = limitf(sp, sp + cfg.sleepBoost, cfg.spBoostMax);

    if (in.psiValid) psiUse = in.psi;        // else hold the last good value

    bool flowIdle = !in.flowValid || (in.flowGPM < cfg.idleGPM);

    // ------------------------------------------------- 2. CAP  (100 ms tick)
    capTableOK = (cfg.capPts >= 2) && (cfg.capPts <= 4) && (cfg.capHzPt[0] > cfg.minHz);
    for (int i = 0; i < cfg.capPts - 1; i++)
      if (cfg.capPsi[i+1] <= cfg.capPsi[i] || cfg.capHzPt[i+1] < cfg.capHzPt[i])
        capTableOK = false;

    tCap += dt;
    while (tCap >= 0.1f) {
      tCap -= 0.1f;
      if (!init) { psiFilt = psiUse; capHz = cfg.capHzPt[0]; init = true; }

      // 300 ms filter -- a protective limit must not chase transducer noise
      psiFilt += 0.3f * (psiUse - psiFilt);

      float target;
      if (!cfg.capEnable)                            target = cfg.maxHz;
      else if (!capTableOK || !in.psiValid)          target = cfg.capHzPt[0];  // safest
      else if (psiFilt <= cfg.capPsi[0])             target = cfg.capHzPt[0];
      else if (psiFilt >= cfg.capPsi[cfg.capPts-1])  target = cfg.capHzPt[cfg.capPts-1];
      else {
        target = cfg.capHzPt[0];
        for (int i = 0; i < cfg.capPts - 1; i++)
          if (psiFilt >= cfg.capPsi[i] && psiFilt < cfg.capPsi[i+1]) {
            float span = cfg.capPsi[i+1] - cfg.capPsi[i];
            target = cfg.capHzPt[i]
                   + (cfg.capHzPt[i+1] - cfg.capHzPt[i]) * ((psiFilt - cfg.capPsi[i]) / span);
          }
      }

      // down 10 Hz/s, up 3 Hz/s: protection is prompt, recovery is deliberate
      d.capTarget = target;
      if (capHz > target) capHz = fmaxf(target, capHz - 1.0f);
      else                capHz = fminf(target, capHz + 0.3f);
      capHz = limitf(cfg.capHzPt[0], capHz, cfg.maxHz);
    }
    // The one place the speed limits are decided, so the PI clamp, the output
    // clamp and anti-windup can never disagree about what the ceiling is.
    // capHz itself is left alone: it is reported to the HMI as the real
    // cavitation cap whether or not the override is standing on top of it.
    float floorEff = fminf(cfg.minHz, capHz);
    float hiEff    = fminf(capHz, cfg.maxHz);
    if (in.ovrActive) {
      hiEff    = limitf(0.0f, in.ovrHz, 60.0f);
      floorEff = 0.0f;
    }

    // ----------------------------------------------------------- 2b. MANUAL
    // The slider IS the speed command: no PI, no cavitation cap, no sleep, no
    // staging.  An operator dragging a slider has to get the number on the
    // slider, and the house rule is that a command from outside is passed
    // verbatim rather than filtered.
    //
    // Deliberately NOT gated on psiValid.  Everywhere else a dead transducer
    // freezes the loop, because everywhere else the loop is steering on that
    // measurement.  Manual is the mode you need precisely when the transducer
    // is missing or lying -- commissioning a pump, proving a drive, checking
    // rotation.  The operator is the feedback path, so the UI has to say so.
    if (in.manMode) {
      float h = limitf(0.0f, in.manHz, 60.0f);
      hzCmd      = h;
      pidI       = h;         // leave the integrator and the ramp somewhere
      spEff      = psiUse;    // sane, so returning to auto does not jump
      sleepStage = 0;
      lagOn      = false;
      out.hzCmd     = hzCmd;
      out.capHz     = capHz;        // still the REAL cap, reported not applied
      out.shutoffHz = shutoffHz;
      out.spActive  = spActive;
      out.runLead   = in.enable && (h > 0.0f);
      out.runLag    = false;
      out.state     = STATE_MANUAL;
      d.sp = sp; d.spActive = spActive; d.spEff = spEff; d.psiUse = psiUse;
      d.raw = h; d.err = 0; d.pTerm = 0; d.clamp = 0; d.capTarget = capHz;
      return;
    }

    // ------------------------------------------------------------ 3. SLEEP
    // Thresholds measured from the shutoff speed track the setpoint; phase 2
    // gets its own because the charge target has a higher shutoff of its own.
    float shutoffActive = 60.0f * sqrtf(spActive / cfg.shutoffPsiAt60);
    float thr1 = cfg.sleepRelShutoff ? shutoffHz     + cfg.sleepHzMargin : cfg.sleepHz;
    float thr2 = cfg.sleepRelShutoff ? shutoffActive + cfg.sleepHzMargin
                                     : cfg.sleepHz + cfg.sleepHz2;

    bool g1spEff = spEff  >= sp - 0.1f;
    bool g1psi   = psiUse >= sp - cfg.sleepBand;
    bool g1hz    = hzCmd  <= thr1;
    // Sleep is suspended under the override: the gates key off hzCmd, and a
    // hand-dialled speed would otherwise trip them and put the pump to sleep
    // in the middle of a diagnostic run.
    bool sleepP1 = in.enable && in.psiValid && !in.ovrActive
                   && g1spEff && g1psi && g1hz && flowIdle;

    bool g2psi   = psiUse >= spActive - cfg.sleepBand;
    bool g2hz    = hzCmd  <= thr2;
    bool sleepP2 = in.enable && in.psiValid && !in.ovrActive && g2psi && g2hz && flowIdle;

    bool wake = (psiUse <= sp - cfg.wakeDrop)
                || (in.flowValid && in.flowGPM > cfg.wakeGPM);

    tSleepP1 .run(sleepP1 && sleepStage == 0, cfg.sleepDlyS, dt);
    tSleepP2 .run(sleepP2 && sleepStage == 1, cfg.sleepDlyS, dt);
    tSleepMin.run(sleepStage == 2,            cfg.sleepMinS, dt);
    // the charge timeout counts only while still CLIMBING, otherwise it would
    // expire while phase 2 was most of the way through its own hold
    tBoost   .run(sleepStage == 1 && psiUse < spActive - cfg.sleepBand,
                  cfg.boostMaxS, dt);

    switch (sleepStage) {
      case 0:
        if (tSleepP1.q) {
          if (cfg.sleepBoost <= 0.1f) { sleepStage = 2; sleepCycles++; }
          else {
            sleepStage = 1;
            // Start the charge ramp from where the pressure ACTUALLY is, not
            // from the setpoint.  Phase 1 completes with pressure at or above
            // setpoint, so leaving spEff at sp means the first stretch of the
            // charge carries a NEGATIVE error and commands the pump slower --
            // the wrong direction, for as long as the ramp takes to climb past
            // the live reading.  Charging is a request for more pressure; it
            // should never begin by asking for less speed.
            spEff = fmaxf(spEff, psiUse);
          }
        }
        break;
      case 1:
        // Do NOT exit on "phase 1 no longer true" -- the pump runs FASTER
        // during the charge, so that test is expected to fail here.  Demand
        // shows up as pressure going backwards, or as flow.
        if (tSleepP2.q)                                    { sleepStage = 2; sleepCycles++; }
        else if (psiUse < sp - cfg.sleepBand || !flowIdle)    sleepStage = 0;
        else if (tBoost.q) { sleepStage = 0; boostAbandon++; }  // retry later
        break;
      case 2:
        if (wake && tSleepMin.q) sleepStage = 0;
        break;
      default: sleepStage = 0;
    }
    if (!in.enable || !in.psiValid) sleepStage = 0;

    // ------------------------------------------------- 4. PI  (50 ms tick)
    bool enNow = in.enable && in.psiValid;
    if (enNow && !enPrev) {                              // R_TRIG
      spEff = fminf(spActive, psiUse + cfg.spStepPsi);   // no step error on start
      pidI  = floorEff;
    }
    enPrev = enNow;

    if (!enNow) {
      spEff = psiUse; pidI = floorEff; hzCmd = 0.0f; tPid = 0;
    } else if (sleepStage == 2) {
      hzCmd = 0.0f; pidI = shutoffHz; tPid = 0;   // wake from roughly the right speed
    } else {
      // Easing into the charge on its own ramp keeps the boost from arriving as
      // a step on the speed command.
      float rampPsiS = cfg.spRampPsiS;
      if (sleepStage == 1 && cfg.chargeRampS > 0.05f && cfg.sleepBoost > 0.01f)
        rampPsiS = cfg.sleepBoost / cfg.chargeRampS;

      tPid += dt;
      while (tPid >= 0.05f) {
        tPid -= 0.05f;
        if (spEff < spActive) spEff = fminf(spActive, spEff + rampPsiS * 0.05f);
        else                  spEff = spActive;  // setpoint lowered, or charge abandoned

        float err  = spEff - psiUse;
        pidI      += cfg.ki * err * 0.05f;
        float raw  = pidI + cfg.kp * err;
        hzCmd      = limitf(floorEff, raw, hiEff);

        // anti-windup by back-calculation: whatever the clamp took away comes
        // straight out of the integrator, so the loop leaves a limit the
        // moment the error reverses instead of unwinding for ten seconds first
        pidI += (hzCmd - raw);

        d.err = err; d.pTerm = cfg.kp * err; d.raw = raw; d.clamp = hzCmd - raw;
      }
    }

    // -------------------------------------------------- 5. LEAD/LAG STAGING
    bool stageUp = in.enable && in.lagAvail && (sleepStage == 0)
                   && (hzCmd >= capHz - 0.5f)
                   && (psiUse < sp - cfg.stageUpPsi);
    bool stageDown = !in.enable || !in.lagAvail || (sleepStage != 0)
                     || (hzCmd <= cfg.stageDownHz);

    tStageUp.run(stageUp && !lagOn,  cfg.stageUpDlyS,   dt);
    tStageDn.run(stageDown && lagOn, cfg.stageDownDlyS, dt);
    tLagRun .run(lagOn,              cfg.lagMinRunS,    dt);

    if (tStageUp.q) lagOn = true;
    if (tStageDn.q && (tLagRun.q || !in.enable || !in.lagAvail)) lagOn = false;
    if (!in.enable || !in.lagAvail) lagOn = false;

    // ----------------------------------------------------------- 6. OUTPUTS
    hzCmd = limitf(0.0f, hzCmd, in.ovrActive ? hiEff : cfg.maxHz);
    out.hzCmd     = hzCmd;
    out.capHz     = capHz;
    out.shutoffHz = shutoffHz;
    out.spActive  = spActive;
    // An override of 0 Hz means stopped, not "running at zero speed" -- leaving
    // the run bit set would hold the drive energised against a 0 reference.
    out.runLead   = in.enable && in.psiValid && (sleepStage != 2)
                    && !(in.ovrActive && hiEff <= 0.0f);
    out.runLag    = out.runLead && lagOn;

    if      (!in.psiValid)                                out.state = STATE_FAULT;
    else if (!in.enable)                                  out.state = STATE_IDLE;
    else if (sleepStage == 2)                             out.state = STATE_ASLEEP;
    else if (sleepStage == 1)                             out.state = STATE_CHARGING;
    else if (lagOn)                                       out.state = STATE_STAGED;
    else if (spEff < sp - 0.1f)                           out.state = STATE_FILL;
    else if (hzCmd >= capHz - 0.2f && capHz < cfg.maxHz)  out.state = STATE_CAPPED;
    else                                                  out.state = STATE_REGULATE;

    // ------------------------------------------------------- diagnostics only
    d.sp = sp; d.spActive = spActive; d.spEff = spEff; d.psiUse = psiUse;
    d.iTerm = pidI; d.floorEff = floorEff; d.psiFilt = psiFilt;
    d.shutoffSP = shutoffHz; d.shutoffActive = shutoffActive;
    d.thr1 = thr1; d.thr2 = thr2;
    d.iAtFloor = (pidI <= floorEff + 0.05f);
    d.iAtCap   = (pidI >= fminf(capHz, cfg.maxHz) - 0.05f);
    d.flowIdle = flowIdle; d.wake = wake;
    d.g1spEff = g1spEff; d.g1psi = g1psi; d.g1hz = g1hz;
    d.g2psi = g2psi; d.g2hz = g2hz;
    d.tP1 = tSleepP1.acc; d.tP2 = tSleepP2.acc;
    d.tMin = tSleepMin.acc; d.tBst = tBoost.acc;
    d.tUp = tStageUp.acc; d.tDn = tStageDn.acc; d.tLag = tLagRun.acc;
    d.stageUp = stageUp; d.stageDown = stageDown; d.lagOn = lagOn;
  }

private:
  float hzCmd = 0, capHz = 40.0f, shutoffHz = 0, psiUse = 0;
  float tCap = 0, tPid = 0;
  bool  init = false, enPrev = false, lagOn = false;
  Ton   tSleepP1, tSleepP2, tSleepMin, tBoost, tStageUp, tStageDn, tLagRun;
};

static inline const char* stateName(int s) {
  switch (s) {
    case STATE_IDLE:     return "idle";
    case STATE_FILL:     return "fill";
    case STATE_REGULATE: return "regulate";
    case STATE_CAPPED:   return "capped";
    case STATE_CHARGING: return "charging";
    case STATE_ASLEEP:   return "asleep";
    case STATE_STAGED:   return "staged";
    case STATE_FAULT:    return "FAULT";
    case STATE_MANUAL:   return "MANUAL";
  }
  return "?";
}
