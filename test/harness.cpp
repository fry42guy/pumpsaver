/*
  Desktop harness -- runs PumpControl against PlantSim with no hardware.

  This is the bench the control block was designed for: pure block in, plant
  model behind it, so gains can be swept and the sleep/staging state machines
  exercised in a second instead of an afternoon.

    harness.exe [demandGPM] [runSeconds] [subStep] [setpoint]
    harness.exe 1.5 400 0.01 55

  Prints every state and sleep-stage transition, then reports which sleep gate
  is holding it awake if it never slept.
*/
#include <cstdio>
#include <cstdlib>
#include "../pump_control.h"
#include "../plant_sim.h"

int main(int argc, char **argv) {
  float demand = (argc > 1) ? (float)atof(argv[1]) : 0.0f;
  float runS   = (argc > 2) ? (float)atof(argv[2]) : 400.0f;
  float sub    = (argc > 3) ? (float)atof(argv[3]) : 0.01f;
  float sp     = (argc > 4) ? (float)atof(argv[4]) : 55.0f;

  PumpControl pc; PumpIn in; PumpOut out; PlantSim pl;
  pc.reset();
  pl.demandGPM = demand;
  pl.psi = 0.0f;

  in.enable = true; in.psiValid = true; in.setpoint = sp;
  in.flowValid = false; in.lagAvail = true;

  printf("demand %.1f gpm  setpoint %.1f psi  substep %.3f s  run %.0f s\n",
         demand, sp, sub, runS);
  printf("shutoff at setpoint = %.2f Hz\n\n", 60.0f * sqrtf(sp / 84.0f));
  printf("   time   psi    hzCmd   cap   flow  stage  state\n");

  int lastStage = -1, lastState = -1;
  float t = 0, nextSample = 0;
  bool everSlept = false;
  float maxPsi = 0;

  while (t < runS) {
    in.psi     = pl.psi;
    in.flowGPM = pl.flowGPM;
    pc.step(in, out, sub);
    pl.step(out.hzCmd, (out.runLead ? 1 : 0) + (out.runLag ? 1 : 0), sub);
    t += sub;
    if (pl.psi > maxPsi) maxPsi = pl.psi;
    if (pc.sleepStage == 2) everSlept = true;

    bool change = (pc.sleepStage != lastStage) || (out.state != lastState);
    if (change || t >= nextSample) {
      printf("%7.1f %6.1f %7.1f %6.1f %6.1f %5d   %-9s%s\n",
             t, pl.psi, out.hzCmd, out.capHz, pl.flowGPM,
             pc.sleepStage, stateName(out.state), change ? "  <--" : "");
      if (change) { lastStage = pc.sleepStage; lastState = out.state; }
      if (t >= nextSample) nextSample = t + 30.0f;
    }
  }

  printf("\nsleep cycles %u   charge abandons %u   peak psi %.1f\n",
         pc.sleepCycles, pc.boostAbandon, maxPsi);

  if (!everSlept) {
    const PumpDiag &d = pc.d;
    printf("\nNEVER SLEPT -- phase 1 gates at end of run:\n");
    printf("  spEff >= sp-0.1   : %-5s (spEff %.2f, need %.2f)\n", d.g1spEff ? "ok" : "FAIL", d.spEff, sp - 0.1f);
    printf("  psi   >= sp-band  : %-5s (psi %.2f, need %.2f)\n",  d.g1psi ? "ok" : "FAIL", d.psiUse, sp - pc.cfg.sleepBand);
    printf("  hzCmd <= thr1     : %-5s (hz %.2f, need <= %.2f)\n", d.g1hz ? "ok" : "FAIL", out.hzCmd, d.thr1);
    printf("  flow idle         : %-5s\n", d.flowIdle ? "ok" : "FAIL");
    printf("\n  shutoff at setpoint %.2f Hz, threshold %.2f Hz (margin %.2f)\n",
           d.shutoffSP, d.thr1, d.thr1 - d.shutoffSP);
    if (!d.g1hz)
      printf("  -> the loop needs %.2f Hz to hold %.1f psi against %.1f gpm,\n"
             "     which is %.2f Hz above shutoff. That is real demand.\n",
             out.hzCmd, sp, demand, out.hzCmd - d.shutoffSP);
  }
  return 0;
}
