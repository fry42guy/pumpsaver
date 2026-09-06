# PumpSaver

Pressure-control retrofit for a repress pump skid. ESP32-S3 talking Modbus RTU to
Invertek Optidrive E3 drives, with the control logic ported from the CODESYS
`FB_PumpControl` block.

**Current build: Mode 1 (offline skid).** PumpSaver is the only controller. All
parameters come from its own web UI. There is no PLC in this build — see
[ROADMAP.md](ROADMAP.md) for the CAN gateway version.

## Hardware

- Waveshare ESP32-S3-RS485-CAN (1× isolated RS485, 1× isolated CAN, jumper-selectable 120 Ω)
- Optidrive E3 over Modbus RTU on the front RJ45 (pins 7/8)
- Power: USB-C 5 V **or** the 7–36 V screw terminal. USB alone is enough to boot
  and to run the RS485 side — the isolated transceiver is fed by an onboard isolator.
- Pins: RS485 TX 17, RX 18, DE 21

## Build

Arduino IDE 2.x, esp32 core 3.x. Board settings are pinned in `sketch.json`
(ESP32S3 Dev Module · USB CDC On Boot **Enabled** · Flash 16MB · PSRAM OPI).

Libraries: **ESP Async WebServer** and **Async TCP**, both the ESP32Async versions,
plus **PubSubClient** (knolleary) for MQTT. `ModbusMaster` is *not* used — see [DESIGN_NOTES.md](DESIGN_NOTES.md).

Use `.build.ps1` — it passes a persistent `--build-path`, which is the difference
between a **97 s** rebuild and an **11 s** one. The first build of a given board
configuration is always ~97 s: it compiles the ESP32 core (119 files) plus
ESPAsyncWebServer and AsyncTCP (111 files), about 20 MB of objects that never
change afterwards. The sketch itself is only 15 files.

The Arduino IDE keeps its own persistent build folder, so the same rule applies
there — first compile slow, the rest fast. Changing any board menu option (PSRAM,
flash size, USB CDC) changes the FQBN and forces a full rebuild.

First flash: hold BOOT, tap RESET, release BOOT, then Upload.

## Bench testing with no drive

`#define SIM 1` (the default) swaps the Modbus layer for `plant_sim.h`, a pump and
tank model calibrated to the same numbers the control block asserts — 84 psi shutoff
at 60 Hz, ~98 gpm at the cap curve's worst point. You get the whole control loop,
sleep state machine and staging on the bench with no drive, no motor and no water.

1. Flash, open Serial Monitor at 115200. A red SIM banner prints.
2. Join Wi-Fi **FCW-PUMP** / **fullcircle**, browse to `http://192.168.4.1`.
   The AP stays up permanently, including once the board has joined a site
   network — the Network card is where you scan for an SSID and point it at
   an MQTT broker. Telemetry is publish-only; nothing is subscribed, so the
   network cannot start, stop or re-tune a pump.
3. Set **Time scale ×10** and **Demand 0 gpm**. Tap **Enable**.
4. Watch fill → regulate. It settles at 55.0 psi and **48.5 Hz** — exactly the
   shutoff speed. After the two 60 s holds it charges to 60 psi and sleeps.

Time scale compresses the control block *and* the plant together, so a 60 s hold
plays out in 6 s at ×10. The sliders apply as you drag; there is no Apply button.

**A pump under real demand is supposed to stay awake.** Sleep phase 1 only arms
at or below `sleepHz` (50 Hz). At 55 psi the loop needs about **51.4 Hz** to hold
40 gpm, so at that draw it will never sleep, and that is correct behaviour — 40 gpm
is not a trickle. To see a full sleep/wake cycle set demand to **1–2 gpm**.

| Demand | What should happen |
|---|---|
| 0 gpm | sleeps after both holds, stays asleep |
| 1.5 gpm | sleeps, wakes on the 5 psi cut-in, repeats |
| 40 gpm | never sleeps — real demand |

The serial CSV is one line per 100 ms tick:
`t_ms,psi,spAct,hzCmd,cap,shutoff,flow,state,lead,lag,sleep`

## Going to real hardware

Set `#define SIM 0`. Then on each drive, from the keypad:

`P-14 = 101` · `P-12 = 3` · `P-31 = 0 or 1` · `P-36`: address, 115.2 kbaud, 1000 ms
watchdog · `P-16 = t 4-20` · motor data `P-07..P-10`, `P-54`

**Drives are addressed from 2.** Address 1 is the uncommissioned slot — PumpSaver
probes it and warns if a drive is sitting there, which is what a replacement unit
looks like before someone re-addresses it.

`P-36` index 3 (the comms watchdog) is the last line of defence: if PumpSaver dies,
the drive stops on its own. Confirm it is set.

## Files

| File | Purpose |
|---|---|
| `pumpsaver.ino` | I/O, Modbus master, web UI, main tick |
| `pump_control.h` | the control block — pure, no Arduino deps, desktop-compilable |
| `plant_sim.h` | bench plant model |
| `page.h` | the web UI, kept out of the .ino for a build reason (see DESIGN_NOTES) |
| `test/harness.cpp` | desktop co-simulation — runs the real control block with no board |
| `DESIGN_NOTES.md` | decisions and why, open questions |
| `ROADMAP.md` | phases and goal tracking |
| `VERSION.md` | version log and flash record |
