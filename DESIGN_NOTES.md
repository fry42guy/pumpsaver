# Design notes

Decisions, and the reasoning that produced them. Written down so we don't
re-litigate them in six months.

## System context

The existing plant is an **ifm CR1082** — a PDM360 NG programmable graphic display
running Embedded Linux with a CODESYS runtime (2.3, or 3.5 SP8+) and multiple CAN
interfaces. It is *both* the HMI and the PLC: `FB_PumpControl` executes on it.

That single fact rules out "replace the PLC, keep the HMI" — they are one box.

## Two modes

**Mode 1 — offline skid (this build).** PumpSaver is the only controller. No PLC,
no CAN. Parameters from its own web UI.

**Mode 2 — CAN gateway (future).** PumpSaver impersonates the VFD on the CR1082's
CAN bus. The PLC keeps its program and its screens; PumpSaver translates to Modbus
and drives one or two physical pumps.

```
Mode 2:  CR1082 ──CAN──► PumpSaver ──Modbus RTU──► Optidrive E3 ×1..2
```

## Why parameters live in PumpSaver's web UI

A **drive** CAN link carries drive objects: run/stop, speed reference, fault reset,
statusword, actual speed, current, fault code. It has no object for *set pressure*
or *sleep delay* — in the CODESYS source those are `VAR_INPUT`/`VAR` living inside
the CR1082, and they never reach the drive bus.

Three options were considered:

1. **Parameters from PumpSaver's own web UI** ← chosen. Zero PLC changes, and the
   UI already existed.
2. Change the CODESYS program to publish them. Correct engineering, but needs the
   project, the licence, and it breaks the "no programming changes" premise.
3. Hijack existing drive objects (repurpose "max speed" to mean setpoint).
   Rejected — unmaintainable, and the HMI would display a number meaning something else.

**Known consequence of (1):** in Mode 2 the CR1082 keeps running `FB_PumpControl`
regardless, so its `eState`, `rCapHz` and sleep display become fiction. Its screens
will need re-labelling, or the operator will trust a lying display at 2 a.m.

## Why not the ModbusMaster library

`ModbusMaster`'s response timeout is `static const uint16_t ku16MBResponseTimeout = 2000`
— a private compile-time constant. With a drive absent that stalls the control loop
for seconds per transaction, and drive discovery has to probe addresses that are
*deliberately* empty. The local `Rtu` class in `pumpsaver.ino` is ~90 lines and lets
the timeout be 150 ms, which is still ~30× what a healthy E3 needs at 115200.

This also becomes a hard prerequisite for Mode 2: as a CANopen slave you must produce
heartbeats on schedule, and a 2 s blocking read drops them.

## Drive addressing

Real drives start at **address 2**; **address 1 is the uncommissioned slot**. A
factory-defaulted E3 answers at 1, so that convention makes "found something at 1"
mean exactly one thing: a drive nobody has commissioned yet — almost always a
replacement unit.

**Discovery scans a range and records which addresses answered.** It does not stop
at the first silence: that would report zero drives when drive 2 is merely powered
down, and would silently drop to single-pump operation if drive 3 died in service.
A CRC error at an address is reported distinctly from silence — it means something
*is* there and the bus wiring is suspect (A/B swap, baud, termination).

Auto-addressing was considered and dropped. It is gated on an unanswered question
(does the E3 accept Modbus parameter writes when `P-12 = 0`?), the `P-36` sub-index
encoding is undocumented, and doing it automatically at boot risks re-addressing a
working drive. A human commissioning each drive is fine.

**Drives boot slower than the ESP32.** Discovery waits 3 s, and Rescan exists
because that still may not be enough in a panel where everything powers at once.

## Safety decisions

- **`gEnable` is never persisted.** In the original sketch it was saved to NVS on
  every start/stop, so a pump would restart itself when power came back. Now enable
  is RAM-only and defaults false.
- **`xPsiValid` freezes the loop and stops the pumps**, ported faithfully. PumpSaver
  generates it from comms health, drive trip state, and a range check on the raw count.
- **Cap parks at row 1 when the table is invalid or pressure is untrusted** — the
  safest speed, not the fastest.
- The safety chain (E-stop, DI1 permissive) must drop the drive independent of
  PumpSaver. Never route a safety function through the adapter.

## What the earlier Arduino sketch had lost

The first port (`fcw_pump_retrofit.ino`) dropped logic that is now restored:

| `FB_PumpControl` | old sketch | why it matters |
|---|---|---|
| cap slews 10 Hz/s down, 3 Hz/s up | instant | cap steps became speed steps |
| 300 ms filter on cap pressure | none | a protective limit chasing transducer noise |
| back-calculation anti-windup | crude clamp | slow to leave a limit when error reverses |
| integrator preload (floor on enable, shutoff on wake) | `integ = 0` | pressure sag on every wake |
| `xPsiValid` | absent | no transducer validation at all |
| phase-1 sleep gated on fill-complete + flow-idle | neither | slept during fill; blind to a trickle |
| staging, flow inputs, `shutoffHz` | absent | — |

It also had a JS bug: `<b id="cmd">` collided with `function cmd()`, so the
Cmd/Cap tile never updated. Fixed here by renaming the id.

## Plant model calibration

`plant_sim.h` is calibrated against two numbers the control block already asserts,
so it is not an arbitrary toy:

- `shutoffPsi60 = 84` reproduces `shutoffHz = 60·√(SP/84)` — 55 psi → 48.55 Hz,
  60 psi → 50.7 Hz, matching the sleep comment.
- `qMax60 = 142 gpm` puts the stock cap table's worst point near 98 gpm (the bench
  figure it was built from) and places a 2 gpm draw at 55 psi at 48.56 Hz against a
  48.55 Hz shutoff — the trickle case the sleep hold exists to bound.

`capacityGalPsi` is the one number to trim to the real skid.

## Open questions

| # | Question | Blocks |
|---|---|---|
| 1 | Does the E3 answer Modbus parameter access with `P-12 = 0`? | auto-addressing (parked) |
| 2 | `P-36` sub-index encoding over Modbus | setting address/baud remotely |
| 3 | `P-16` encoding — cannot be read back, so the 4-20 mA format is an unverified keypad step | transducer fault protection |
| 4 | What protocol is on the CR1082's CAN bus — CANopen CiA 402, or Invertek proprietary? | Mode 2 scope |
| 5 | How many drive nodes in the CR1082's CANopen config — one or two? | who owns staging in Mode 2 |
| 6 | Is the EDS file and CODESYS project available? | Mode 2 |
| 7 | Does the CR1082 have an RS485 port? | if yes, Mode 2 may be unnecessary |
| 8 | Transducer on drive AI1, or direct into PumpSaver? | feedback latency, single point of failure |

On #8: reading it through the drive puts the Modbus poll inside the control loop's
feedback path and makes one drive a single point of failure for the whole skid.
Reading 4-20 mA directly at PumpSaver (ADS1115 + 165 Ω burden — *not* the ESP32's
internal ADC) fixes both. Alternatively wire the loop in series through both drives'
AI1 so either can provide pressure.
