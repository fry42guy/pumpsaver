# Working rules for this repo

## Version bump — every pass, no exceptions

Before finishing any change pass that touches firmware:

1. Bump `FW_VERSION` in `pumpsaver.ino`.
   - patch (`0.1.0` → `0.1.1`) — fixes, tuning, comments, docs-only firmware touches
   - minor (`0.1.x` → `0.2.0`) — new feature, new setting, changed web API
   - major (`0.x` → `1.0.0`) — first field-deployed build, then breaking changes only
2. Add a row to `VERSION.md` — version, date, what changed, and whether it was
   flashed to hardware.
3. Compile before claiming done — **always via `.\build.ps1`**, never a bare
   `arduino-cli compile`. arduino-cli does not keep its temp build directory
   between runs, so a bare compile rebuilds the ESP32 core and the async
   libraries every single time: 97 s instead of 11 s. `build.ps1` passes a
   stable `--build-path`.
   ```
   .\build.ps1                 # ~11 s warm, ~97 s the first time
   .\build.ps1 -Upload         # compile then flash COM5
   .\build.ps1 -Clean          # force a full rebuild
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
- **Never give an element an `id` that shadows a global.** A bare `id` is reachable
  as a window property, but real window properties win — `id="alert"` resolves to
  `window.alert`, not the element. Same trap that once stopped the Cmd tile updating.

- **A form must be able to read back every field it writes.** If a `/xxx` GET does
  not return a field its POST accepts, the form renders it empty and saving writes
  `"".toFloat()` = 0 over the stored value. This silently disabled the CAN
  gateway's P-01/P-02 enforcement until v0.10.0.

- **`SIM` is set by `build.ps1 -Sim`, never by editing the define.** It writes a
  generated, gitignored `sim_flag.h`, so the demo and real-drive binaries come
  from one commit — which is what makes a board matchable to a commit.

- **In `build.ps1`, test `$LASTEXITCODE`, never `$?`.** In Windows PowerShell 5.1 a
  native command that writes anything to stderr — a compiler warning is enough —
  sets `$?` false even on exit code 0.

- **The web page lives in `page.h`, never in the `.ino`.** Arduino generates C++
  prototypes by scanning `.ino` files and does not understand raw string literals,
  so a line like `function foo(){` inside `R"HTML(...)HTML"` breaks the build with
  *"'function' does not name a type"*. `.h` files are not preprocessed. `page.h` is
  the hub; the screens are `page_home.h`, `page_pump.h`, `page_sim.h`,
  `page_net.h` and `page_system.h`, all sharing `ui_common.h`.

- **Logging must never be able to stall control.** A USB CDC write blocks while
  the host is not draining the port, and the CSV line is written from inside the
  control tick. Measured before the fix: 401 ms average tick with a 24 s stall.
  `Serial.setTxTimeoutMs(0)` in `setup()` — never remove it.

- **Every endpoint that reports live state sends `Cache-Control: no-store`**
  (`sendNoCache()`). Without it a client may serve a stale reading, and a page
  showing a seconds-old pressure with no indication is indistinguishable from a
  controller that has stopped.

- **A pump profile carries the PUMP, never the installation.** `pump_profile.h`
  applies curve, limits, cap table and nameplate; it deliberately leaves
  setpoint, sleep and staging alone. Copying one site's setpoint onto another
  site's pump is how you commission to the wrong pressure.

- **If the link fails with core symbols undefined** (`micros`, `String`,
  `app_main`), the arduino-cli build directory is stale. Delete
  `%LOCALAPPDATA%\Temp\pumpsaver-build*` and rebuild; `-Clean` alone is not
  always enough.

## Build environment

Arduino IDE 2.x, esp32 core 3.3.11. Board: ESP32S3 Dev Module, USB CDC On Boot
Enabled, Flash 16MB, PSRAM OPI (pinned in `sketch.json`). Libraries: ESPAsyncWebServer
+ AsyncTCP (ESP32Async). A full build takes ~90 s — it is not hung.

