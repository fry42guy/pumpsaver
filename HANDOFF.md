# Handoff — pick up here

Living document. **Update it at the end of a session, not the start.**
Read `CLAUDE.md` first (the non-negotiable rules), then this.

Last updated: **2026-09-08**, at `v0.20.0`.

---

## State right now

| | |
|---|---|
| Firmware on the bench board | `v0.20.0`, real-drive binary |
| Board device id | `5519C4` (MAC-derived — see the `000000` fix in `VERSION.md`) |
| Simulation | **OFF** — talking to a real drive |
| Real drive | answers at **Modbus address 2**, reads + writes verified, `0x0040` ready |
| Station Wi-Fi | **cleared** (`sta clear`). AP only: **FCW-PUMP**, http://192.168.4.1 |
| MQTT | staged for ThingsBoard, **needs station Wi-Fi again before it can publish** |

**A serial bench console now exists** — it is how most of this session was driven.
Plug in USB and type `help`. It matters because a laptop with one radio cannot join
FCW-PUMP without giving up the network it is already on.

> Station Wi-Fi was cleared because a configured-but-absent SSID makes the station
> retry forever, and the softAP follows it across channels — so the AP is hardest to
> join exactly when it is the only way in. Set an SSID again before any MQTT work.

## The E3, now that we have the manual

`MODBUS_E3.md` is the reference — **read it before writing any drive code.** It exists
because this session lost hours to three wrong guesses, all now recorded there:

- **Function 16 works on registers 1–4 only.** Every parameter write failed for this
  reason and looked like a locked drive. Parameter writes need **function 06**.
- **Registers 129/130 are NOT P-01/P-02.** The `128 + P` formula is documented for
  **P-04 upward only**. The keypad reads P-02 = 18.0 while register 130 reads 3480.
- **P-12 = 3 was never the fault** — 3 and 4 are both valid Modbus control modes.

Two things from the manual we have **not** verified on this drive:

- **P-31 must be 0 or 1** or Modbus run/stop silently stays with the control terminals.
  Never read it. Worth two minutes on the keypad.
- **P-12 = 3 still requires the hardware enable on DI1** (terminals 1–2 linked).

**Register 2006 is motor torque.** This file's own flow-estimation section called that
the highest-leverage open item; it was in the map all along, and the sweep captures it
now along with motor volts (2014), DC bus (23) and power (2004).

## The drive ignores a low speed reference — solved

Symptom: the board commanded 4.5 Hz, the motor ran 54. The board was right the whole
time; `/status` echoes registers 1 and 2 back from the drive so the gap is visible now.
Cause is **P-02 minimum speed**, currently **18.0 Hz** — the drive clamps our reference
up to its own floor. **Set P-02 = 0 for the manual slider to have authority below 18 Hz.**

---

## Next, in order

1. **Set P-02 = 0 on the keypad** (P-14 = 101 unlocks). Nothing below 18 Hz works until then.
2. **Run both sweeps on `/cal`** — deadhead, then wide open with gpm typed in per row.
   Download the CSV. That data yields `shutoffPsiAt60` (currently an **unverified 84**
   feeding the cap table, the sleep thresholds and the envelope plot), suction pressure
   from the same fit, the process gain that sets `kp`, and a measured cap table.
3. **Then** tune. Agreed and not yet done: filter the PI feedback (it reads raw pressure
   while only the cap gets the low-pass), the cap ramp-up rate (3 Hz/s up vs 10 down),
   shorter sleep timings for bench work, and the drive's own P-03 accel of 5.0 s —
   probably the largest single contributor to "slow to wind up", and a drive parameter,
   not firmware.

Tuning before step 2 means fitting gains to a curve nobody has measured.

## Known, unexplained

- **`/pump` sometimes will not load.** Blamed on heap fragmentation, then measured:
  163 KB largest free block against a 41 KB page. **That theory was wrong.** Pages moved
  to `send_P` anyway (streams from flash, no 40 KB heap copy) but the root cause is open.
- **VFD noise kills the USB console whenever the motor runs.** Three times, including
  mid-test with the port dropping entirely. A ferrite on the USB lead and routing it away
  from the motor cable would help; for anything with a pump turning, prefer the web UI.
- **The transducer reads garbage.** 0.2–6.8 psi dithering at rest, and 26.2 psi observed
  while the motor turned 9.6 Hz — where affinity from an 84 psi shutoff allows about 2.1.
  `gPsiValid` is **true** throughout: `ai <= 1000` accepts 0 counts as a valid 0 psi, so a
  failed-open 4–20 mA loop is indistinguishable from a real reading. That is the case
  where a pump runs to cap chasing a setpoint it cannot see. **Still unfixed.**

## The one live thread

**Finish the ThingsBoard test.** Everything is staged; one field is missing.

1. Open `/network` on the board.
2. Paste the ThingsBoard **device access token** into **Username**.
3. Leave **Password** blank. Save.

Already proven: DNS resolves `mqtt.thingsboard.cloud`, TCP connects, and
ThingsBoard returns a proper MQTT CONNACK rejection (`rc 2`) for the anonymous
attempt. Only authentication is missing.

Two caveats:
- **Port 1883 is plaintext.** The token and every reading cross the internet in
  the clear. Fine for a bench test. Production wants 8883 + TLS, which means
  swapping `WiFiClient` for `WiFiClientSecure` and carrying a CA bundle.
- Publish interval is set to **10 s** (8,640 msgs/day) to stay clear of free-tier
  message quotas. It was 2 s during testing.

---

## Open, in the order I would do them

### 1. Warn when a setpoint is physically unregulatable
At a 21 psi setpoint with `minHz` 30, the shutoff speed *is* 30.0 Hz — the loop
has no authority below its floor and short-cycles forever, with `sleepCycles`
climbing about once a second and nothing on screen explaining why.
`shutoffHz` is already in `/status`, so this is a live warning beside the
setpoint plus a shaded "this pump can regulate here" band on the slider.
**Cheapest change with the largest effect. There is currently not one advisory
warning anywhere in the UI.**

### 2. Plain-language state
`At cap`, `Charging`, `Both pumps` are engineer words. One line under the state
pill saying what it means and what happens next. Real example from testing: at
110 gpm the header collapsed to 0 psi, and the cavitation table caps speed to
40 Hz at 0 psi, so it could not rebuild pressure until demand dropped. Correct
protection — but the screen only said "At cap".

*1 and 2 are small, low-risk, and share one build-and-flash. Do them together.*

### 3. Progressive disclosure on the Pump page
38 separate fields on one screen. An installer cannot tell that six matter and
the rest come from the profile. Split into Essential / Tuning / Advanced with
the last two collapsed. Pure UI, no control-path risk.

### 4. The pump profiling wizard
Designed in detail (see below), not built. Turns "fill in 38 fields correctly"
into "answer five questions while I run the pump". Also the strongest sales
line available: *you don't configure it, it profiles itself.*

### 5. OTA
`app3M_fat9M_16MB` already carries `app0` **and** `app1` at 3 MB each plus
`otadata`. OTA needs no partition change and costs no application space — only
the update code and a way to serve the image.

---

## Flow estimation — the conclusion, so it is not re-derived

**Question:** can flow be estimated from pressure, Hz and amps?
**Answer:** yes, mid-range, ±10–15 %. `flowAt()` in the `.ino` already does the
pressure+speed half.

What the analysis actually found:

- **Amps adds less than the textbook suggests.** On a part-loaded motor the
  magnetizing current dominates: the whole 0→40 gpm swing is 0.73 A, so the
  drive's own measurement error eats a sixth of the signal. Pressure beats
  current everywhere except below ~8 gpm, where both are useless.
- **Check the E3 register map for torque % or output power.** We read only 6, 7,
  8, 20, 24. Torque carries no magnetizing offset and the drive computes it
  knowing the power factor — worth about ±4.5 gpm against current's ±11.
  **Highest-leverage open item on this topic.**
- **It cannot make the sleep decision.** 2 gpm vs 0 gpm is 0.01 Hz and 0.025 A,
  under the resolution of both instruments. `DESIGN_NOTES.md` is right.
- **Amps earns its keep as a change detector**, not a flow meter. Deviation from
  a calibrated `I(P, Hz)` surface is impeller wear, a dragging bearing or a
  closing valve. Arguably worth more than the flow number.

### Calibration: 14 points, no flow meter needed
Closing a valve gives a flow-labelled point for free (Q = 0 exactly).

- **Deadhead** at 30 / 40 / 50 / 60 Hz — record P and A.
- **Full open** at 40 / 50 / 60 Hz — record P and A.

Deadhead steps **must** be time- and amp-limited (30–60 s, auto-abort): a
deadheaded pump cooks its seal, and full shutoff head has to be inside the
system's pressure rating.

**Suction pressure falls out of the same data.** At deadhead
`P_meas(f) = P_suction + S·r²`, so regressing `P_meas` against `r²` gives the
true shutoff head as the slope and **suction pressure as the intercept** —
±0.6 psi with four speeds. It also re-measures itself forever with nobody
involved: `sleepStage == 1` (the charge phase) *is* a deadhead, and near
shutoff the curve is flat enough that 10 gpm of leakage moves head by 0.42 psi.

Why bother when suction "will not change much": if it does drop, NPSH available
drops with it, cavitation onset moves to a lower flow, and **the cap table
becomes optimistic** — the pump cavitates at a speed the table still calls safe.
Nobody sees a wrong number; they hear gravel, eventually.

The wizard's arithmetic should be pure functions in `pump_control.h` style so
`test/harness.cpp` proves it on the desktop first. **No host C++ compiler is
installed on the bench laptop** — MinGW or Build Tools, ~10 min, before that
can happen.

---

## Bench tools

**Serial console** — USB CDC, type `help`. Non-blocking, polled before the tick gate,
so it cannot stall control:

```
log on|off          the 10 Hz CSV stream (mute it to read anything)
status              firmware, mode, override, drives, register echo
scan | regs [addr]  rescan the bus | raw register dump
hold on|off         stop the control tick writing, for a raw probe
rd <a> <r> [n]      read registers      wr <a> <r> <v>   write one (needs hold)
man off|<hz>        MANUAL speed, PI and cap bypassed
ovr off|<pct>       max-Hz override
run | stop | reset  enable | disable | clear a drive trip
sweep dead|open|next|abort|show
sta clear           blank the station SSID (escape a retry storm)
heap | reboot
```

```
.\build.ps1              compile          ~11 s warm, ~97 s cold
.\build.ps1 -Upload      compile + flash COM5
.\build.ps1 -Sim         demo binary (generated, gitignored sim_flag.h)

node test/render_pages.js   assemble the PROGMEM pages, check tags + JS syntax
node test/id_collide.js     enforce the CLAUDE.md id-shadowing rule
node test/mqtt_broker.js    dependency-free MQTT broker on :1883, prints payloads
```

Run the two page checks before any flash that touched a `page_*.h` — they cost
seconds and catch what otherwise costs a build cycle plus a browser that
silently does nothing.

> `id_collide.js` was rewritten on 2026-09-08. The previous version built its
> patterns from plain strings containing `'\b'`, which is a **backspace
> character**, not a word boundary — so every pattern demanded a literal 0x08
> after the id, matched nothing, and reported every page clean while checking
> precisely nothing. It now counts what it examined and fails if that is zero.
> Use `String.raw` in this file.

`test/mqtt_broker.js` is how the v0.14.0 telemetry was verified — point the
board's broker host at the machine's LAN address and watch the JSON arrive.
Requires the board in **station** mode; MQTT does not publish from the AP.

---

## Things that have bitten, so they do not bite twice

- **COM5 "busy or doesn't exist" on upload** is usually transient. Wait a few
  seconds and retry; it took three attempts once. Confirmed again this session.
- **Every reflash trips the drive.** Modbus goes silent for ~30 s and the P-36
  watchdog does its job — trip **50**, `SC-F01`. Not a fault: run `reset`.
- **Building from the Arduino IDE with USB CDC On Boot disabled** fails as
  *"'class HardwareSerial' has no member named setTxTimeoutMs"*, which points nowhere
  near the cause. A `#error` now names the menu option. Deleting that call to silence
  it would produce a board that flashes fine and comes up mute on USB.
- **A `<button id="x" onclick="x()">` shadows its own function.** The id-collision
  linter caught this twice in one session. Name the element `xBtn`.
- **A 1 Hz poll that rewrites `innerHTML` destroys the input being typed into.**
  Render only when the data changed and nothing inside has focus.
- **Undefined core symbols** (`micros`, `String`, `app_main`) at link time means
  a stale build dir. Delete `%LOCALAPPDATA%\Temp\pumpsaver-build*`; `-Clean`
  alone is not always enough.
- **Shell heredocs mangle backslashes and apostrophes.** Prose and regex-heavy
  files have been corrupted this way repeatedly. Use the file-writing tools.
- **The AP drops connections under rapid sequential requests.** Hit repeatedly
  from a PowerShell test loop. Worth checking AsyncTCP's connection limits
  before demoing to a room full of people refreshing phones.
- **`$?` is false after any native stderr output** in Windows PowerShell 5.1 —
  a compiler warning is enough. `build.ps1` tests `$LASTEXITCODE`.

---

## Where the reasoning lives

| File | What it holds |
|---|---|
| `CLAUDE.md` | non-negotiable rules and why each exists |
| `MODBUS_E3.md` | the E3 register map, control word, parameter access, trip codes |
| `DESIGN_NOTES.md` | control decisions; why speed is a bad proxy for flow |
| `ECOSYSTEM.md` | multi-node addressing, the standalone rule, roles, build order |
| `VERSION.md` | every version, what changed, whether it was flashed |
| `ROADMAP.md` | phases |
