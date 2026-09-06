# Version log

Bump `FW_VERSION` in `pumpsaver.ino` on every change pass. See `CLAUDE.md`.

| Version | Date | Flashed | Change |
|---------|------|---------|--------|
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
