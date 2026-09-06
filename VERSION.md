# Version log

Bump `FW_VERSION` in `pumpsaver.ino` on every change pass. See `CLAUDE.md`.

| Version | Date | Flashed | Change |
|---------|------|---------|--------|
| 0.5.0 | 2026-09-06 | not yet | Sim can be driven by **pressure** as well as demand: pressure mode holds the header at a dialled-in psi (infinitely stiff source) so the loop reacts to it directly — sweeps the cap curve and flips the sleep gates without waiting on tank dynamics. |
| 0.4.0 | 2026-09-06 | not yet | Diagnostics dashboard (`/diag`) — PI terms, integrator pinning, anti-windup activity, every sleep and staging gate, and timer progress bars. **Fix:** a fixed `sleepHz` made sleep impossible above ~62 psi setpoint; the threshold now sits a margin above shutoff speed and tracks the setpoint (`sleepRelShutoff`). Added `chargeRampS` (3 s default) to ease into the charge. Flow-meter toggle exposed. Pump-curve help text plus a solver for free-flow from one measured point. Page moved to `page.h`. NVS magic → 0x50535635, settings reset. |
| 0.3.0 | 2026-09-05 | not yet | **Fix:** sim integrated the whole tick in one Euler step, so at time scale ×60 pressure overshot to 378 psi instead of settling at 60. Control and plant now co-simulate in ≤10 ms sub-steps. Sim controls are sliders that apply while dragging (debounced) and on release — no Apply button — each with a value box and adjustable range. Added `test/harness.cpp`, a desktop co-simulation of the real control block. |
| 0.2.0 | 2026-09-05 | not yet | Operating-envelope plot on the web page: cap curve, shutoff curve, true constant-flow contour at cavitation onset, live operating point and trail. New settings `qMax60` and `cavOnsetGPM` (display/reference only, not used by the control block). NVS magic bumped to 0x50535634 — **settings reset to defaults on first boot of this version**. |
| 0.1.0 | 2026-09-05 | not yet | First PumpSaver build. `FB_PumpControl` ported to `pump_control.h` as a pure block; `plant_sim.h` bench model; local Modbus RTU master with a 150 ms timeout; drive discovery over a range; web UI owns all parameters. SIM defaults to 1. |

## Versioning

- **patch** — fixes, tuning, comments
- **minor** — new feature, new setting, changed web API
- **major** — first field deployment, then breaking changes only

## Flash record

Record what actually went onto hardware, so a board in the field can be matched
to a commit. The running version is on the serial banner, the page header, and
`/status`.

| Version | Date | Board / skid | Notes |
|---------|------|--------------|-------|
| — | — | — | nothing flashed yet |
