#pragma once
#include <math.h>

/*
  PlantSim -- a stand-in repress skid, so PumpControl can be exercised and
  its gains tuned with no drive, no motor and no water attached.

  Pump curve from the affinity laws off a quadratic head curve:
      shutoff head  H0(f) = shutoffPsi60 * (f/60)^2
      delivery      Q(f,P) = qMax60 * (f/60) * sqrt(1 - P/H0)

  Calibrated against the two numbers the control block already asserts:

    * shutoffPsi60 = 84 psi reproduces  shutoffHz = 60*sqrt(SP/84),
      so 55 psi -> 48.55 Hz and 60 psi -> 50.7 Hz, matching the comment
      in the sleep section.

    * qMax60 = 142 gpm puts the stock cap table's worst point at about
      98 gpm, which is the bench figure the table was built from, and it
      places a 2 gpm draw at 55 psi at 48.56 Hz against a 48.55 Hz
      shutoff -- the trickle case the sleep hold exists to bound.

  capacityGalPsi is the one number to trim to your real skid: it is the
  bladder tank plus pipe volume, in gallons of water per psi.  An 80 gal
  tank pre-charged to 40 psi works out near 1 gal/psi around setpoint.
  Smaller value = twitchier system = harder on the gains.
*/
struct PlantSim {
  // ---- plant constants
  float shutoffPsi60  = 84.0f;   // no-flow head at 60 Hz
  float qMax60        = 142.0f;  // gpm at zero head, 60 Hz
  float capacityGalPsi = 1.5f;   // system capacitance
  float leakGPM       = 0.0f;    // constant background loss
  float rampHzS       = 12.0f;   // drive accel/decel, Hz per second

  // ---- live state
  float psi     = 0.0f;
  float hzAct   = 0.0f;          // actual speed, ramps toward command
  float flowGPM = 0.0f;          // total delivered, all running pumps
  float amps    = 0.0f;

  // ---- what the bench operator drives
  float demandGPM = 0.0f;        // draw off the header

  float pumpFlow(float hz, float p) const {
    if (hz < 1.0f) return 0.0f;
    float r  = hz / 60.0f;
    float h0 = shutoffPsi60 * r * r;
    if (p >= h0) return 0.0f;                    // deadheaded, no delivery
    return qMax60 * r * sqrtf(1.0f - p / h0);
  }

  void step(float hzCmd, int pumpsRunning, float dt) {
    if (pumpsRunning < 1) hzCmd = 0.0f;

    // drive ramp
    float dmax = rampHzS * dt;
    if (hzAct < hzCmd) hzAct = fminf(hzCmd, hzAct + dmax);
    else               hzAct = fmaxf(hzCmd, hzAct - dmax);

    flowGPM = pumpFlow(hzAct, psi) * (float)(pumpsRunning > 0 ? pumpsRunning : 0);

    float net = flowGPM - demandGPM - leakGPM;
    psi += (net / capacityGalPsi) * dt;
    if (psi < 0.0f) psi = 0.0f;

    // plausible motor current for the display
    amps = (hzAct < 1.0f) ? 0.0f
         : 1.8f + 0.055f * hzAct + 0.02f * (flowGPM / (pumpsRunning ? pumpsRunning : 1));
  }
};
