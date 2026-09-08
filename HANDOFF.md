# Handoff — pick up here

Living document. **Update it at the end of a session, not the start.**
Read `CLAUDE.md` first (the non-negotiable rules), then this.

Last updated: **2026-09-08**, at `v0.14.0` (`5113762`).

---

## State right now

| | |
|---|---|
| Firmware on the bench board | `v0.14.0`, real-drive binary (`SIM 0`) |
| Board device id | `5519C4` (MAC-derived — see the `000000` fix in `VERSION.md`) |
| Simulation | **ON**, 2 pumps, demand 110 gpm, setpoint 55 psi |
| Station Wi-Fi | joined `google`, was `192.168.1.44`, `pumpsaver.local` |
| MQTT | pointed at `mqtt.thingsboard.cloud:1883`, ThingsBoard mode ON, **access token not yet entered** |
| Repo | `master` == `origin/master`, clean tree |

**Away from that network the board will not find `google`**, so `pumpsaver.local`
will not resolve. Its own AP is always up: join **FCW-PUMP** and use
**http://192.168.4.1**. That is by design — the AP is how you reach a board
while standing in front of it, whatever the site network is doing.

> The bench Wi-Fi password is stored in the board's NVS (it was needed to reach
> a broker). Blank the SSID field on `/network` to remove it.

---

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
  seconds and retry; it took three attempts once.
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
| `DESIGN_NOTES.md` | control decisions; why speed is a bad proxy for flow |
| `ECOSYSTEM.md` | multi-node addressing, the standalone rule, roles, build order |
| `VERSION.md` | every version, what changed, whether it was flashed |
| `ROADMAP.md` | phases |
