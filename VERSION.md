# Version log

Bump `FW_VERSION` in `pumpsaver.ino` on every change pass. See `CLAUDE.md`.

| Version | Date | Flashed | Change |
|---------|------|---------|--------|
| 0.7.0 | 2026-09-06 | not yet | **Config export / import.** `/export` downloads every setting as named JSON; `/import` accepts a pasted config. Named fields rather than the NVS blob, so a file written today still imports into later firmware — unknown keys ignored, missing keys left alone, everything re-clamped on the way in. **Passwords are never exported**; the network block carries SSID, broker and topic so a replacement unit is one paste from configured, then the two secrets get typed in on the board. Clamp limits moved into one `cfgClamp()` shared by `/set` and import so they cannot drift apart. |
| 0.6.0 | 2026-09-06 | not yet | **Station WiFi + MQTT telemetry.** Radio now `WIFI_AP_STA`: the FCW-PUMP config AP stays up permanently and the station side joins the site network, so the page is reachable whether or not that network works. Network settings live in their own NVS namespace (`psnet`), *not* the settings blob — a broker or SSID change no longer resets pump tuning. WiFi and MQTT run on a dedicated FreeRTOS task on core 0, so a blocking broker connect can never touch the control tick. Telemetry is publish-only with a retained online/offline last will; nothing is subscribed, so the network cannot command a pump. New `/net`, `/scan` endpoints and a Network card with an SSID picker. |
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
