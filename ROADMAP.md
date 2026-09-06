# Roadmap

## High-level goals

| # | Goal | Status | Gate to clear |
|---|------|--------|---------------|
| **G1** | Control logic tunable on the bench with no hardware | **done** | `pump_control.h` pure + `plant_sim.h` — v0.1.0 |
| **G2** | One real drive, one pump, holding pressure | not started | needs a drive on the bench |
| **G3** | Two drives, lead/lag staging, on a real skid | not started | G2 + second drive addressed |
| **G4** | Mode 2 — CAN gateway to the CR1082 | blocked | open questions 4–7 in DESIGN_NOTES |

## Phase 1 — offline skid (current)

- [x] Port `FB_PumpControl` faithfully as a pure block
- [x] Bench plant model calibrated to the block's own asserted numbers
- [x] Local Modbus RTU master with a controllable timeout
- [x] Drive discovery over a range, address 1 as the uncommissioned slot
- [x] Web UI owning every parameter
- [x] Enable no longer persisted across power loss
- [x] Operating-envelope plot with the true cavitation flow contour (v0.2.0)
- [x] Desktop harness — real control block, no board (v0.3.0)
- [x] Verify a full sleep cycle: fill → regulate → charge → asleep → wake
      (harness, 1.5 gpm: 2 cycles; 0 gpm: sleeps and stays; 40 gpm: correctly never sleeps)
- [ ] **Tune Kp/Ki against the sim** and record the result
- [ ] Verify staging up and down with hysteresis, and `lagMinRunS`
- [ ] Bench test against one real drive
- [ ] Confirm `P-36` index 3 watchdog actually stops the drive when PumpSaver dies

## Phase 2 — hardening before anything sees a motor

- [ ] Move Modbus onto its own FreeRTOS task on the second core (also a Mode 2 prereq)
- [ ] Transducer plausibility checks beyond the range check — stuck-value detection
- [ ] Hardware watchdog timer
- [ ] Stall / no-pressure-rise detection (commanded speed, no flow, no pressure)
- [ ] Drive fault auto-retry with a backoff and an attempt limit
- [ ] Runtime hour counters per drive
- [ ] Lead/lag duty alternation on hours, so one pump doesn't wear out first

## Phase 3 — Mode 2, the CAN gateway

Blocked until the open questions are answered. Sequence once they are:

- [ ] Sniff the live CR1082↔VFD CAN bus in listen-only, log every frame
- [ ] Obtain the EDS file — it is the contract, and it beats sniffing
- [ ] Decide one virtual node or two (open question 5)
- [ ] CANopen slave: NMT, boot-up, heartbeat, SDO server, PDO map
- [ ] Fault aggregation strategy — what a virtual VFD reports when only pump 2 trips
- [ ] Define what "current" means with two pumps running (lead only, or sum)
- [ ] Measure added round-trip latency and check it against the PLC's tuning
- [ ] Decide how sleep is represented to a PLC that has no concept of it

## Future features / ideas

- Direct 4-20 mA transducer input (ADS1115 + 165 Ω burden) — removes the Modbus
  poll from the feedback path and kills a single point of failure
- Flow meter input, so the sleep logic can actually see a trickle draw
- OTA firmware update
- Data logging to USB or SD for capturing field runs
- AP-on-demand instead of the radio staying on permanently
- Desktop test harness — compile `pump_control.h` with g++ and run an 8-hour day
  in a second, for gain sweeps
- More than two pumps (needs the control block extended past lead + lag)
- Pressure trend graph on the web page (the envelope plot is spatial, not temporal)
- Iso-flow contours at several flows, not just at onset
- Log the operating point to CSV alongside the existing tick data, so a field run
  can be replayed onto the envelope afterwards

## Explicitly not doing

- **Auto-addressing at boot.** Risks re-addressing a working drive; gated on
  unanswered questions about `P-12` and `P-36`. A commissioning button is enough.
- **Hijacking drive objects to carry setpoint** in Mode 2. Unmaintainable.
- **Filtering or slew-limiting a speed command that came from a PLC.** Inserts phase
  lag into someone else's tuned loop.
- **Two controllers running at once.** In Mode 2 PumpSaver either owns the loop or
  it does not — never both.
