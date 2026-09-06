# Working rules for this repo

## Version bump — every pass, no exceptions

Before finishing any change pass that touches firmware:

1. Bump `FW_VERSION` in `pumpsaver.ino`.
   - patch (`0.1.0` → `0.1.1`) — fixes, tuning, comments, docs-only firmware touches
   - minor (`0.1.x` → `0.2.0`) — new feature, new setting, changed web API
   - major (`0.x` → `1.0.0`) — first field-deployed build, then breaking changes only
2. Add a row to `VERSION.md` — version, date, what changed, and whether it was
   flashed to hardware.
3. Compile before claiming done:
   ```
   & "C:\Program Files\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe" `
     --config-file "C:\Users\RyanBaird\.arduinoIDE\arduino-cli.yaml" `
     compile --fqbn "esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=16M,PSRAM=opi" `
     "C:\Users\RyanBaird\Downloads\pumpsaver"
   ```
4. Commit with the version in the subject: `v0.1.1 — short description`.

A board in the field must always be matchable to a commit. The version shows on
the serial banner, the page header, and `/status`.

## Design rules that are not negotiable

- **`pump_control.h` stays pure.** No Arduino headers, no globals, no I/O. It has
  to compile on the desktop and run against `plant_sim.h`. That is how the gains
  get tuned, and it is inherited from the CODESYS block's own design intent.
- **`gEnable` is never persisted to NVS.** A power cut must not restart a pump.
- **Nothing filters or slew-limits a speed command that came from outside.** A
  hard cap is fine; a filter inserts phase lag into someone else's control loop.
- **Measurements pass through verbatim.** Never report a corrected or invented
  value as if it were measured — it destroys the operator's ability to diagnose.

## Build environment

Arduino IDE 2.x, esp32 core 3.3.11. Board: ESP32S3 Dev Module, USB CDC On Boot
Enabled, Flash 16MB, PSRAM OPI (pinned in `sketch.json`). Libraries: ESPAsyncWebServer
+ AsyncTCP (ESP32Async). A full build takes ~90 s — it is not hung.
