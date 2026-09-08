# Optidrive E3 — Modbus RTU reference

Everything here is from the **Invertek Optidrive ODE-3 IP66 user guide, revision 1.26**
(firmware 3.11), sections 6, 8 and 9. Before this document landed, most of it was
inference from probing a live drive, and **two of those inferences were wrong** — see
*Corrections* at the bottom. Prefer this file over anything remembered.

## Link

| | |
|---|---|
| Protocol | Modbus RTU, CRC |
| Baud | 9600 / 19200 / 38400 / 57600 / **115200 (default)** |
| Format | 1 start, 8 data, 1 stop, **no parity** |
| Physical | RS-485 2-wire, RJ45 on the drive front |
| RJ45 pins | **3** = 0 V, **7** = RS485−, **8** = RS485+ |

> Pins 4/5 are a *different* RS-485 pair (PC/Optibus). Pins 1/2 are CAN. It is not
> Ethernet — do not plug it into a switch.

### Function codes — this one bites

| Code | Function | Restriction |
|---|---|---|
| 03 | Read holding registers | — |
| 06 | Write **single** holding register | — |
| 16 | Write **multiple** holding registers | **Registers 1–4 ONLY** |

**Function 16 outside registers 1–4 is answered with silence.** Every parameter write
this project attempted failed for exactly this reason and was misread as a locked drive.
Parameter writes must use **function 06**.

### Addressing

The manual: *"For master devices which use zero based addressing … it may be necessary
to convert the register numbers by subtracting 1."* Our `REG(n)` macro is `n-1`, which is
correct — register 1 in this table is wire address 0.

## Control and monitor registers

| Reg | R/W | Function | Scaling |
|---|---|---|---|
| 1 | R/W | Control word | bitfield, below |
| 2 | R/W | Frequency setpoint | **Hz × 10**, −5000..5000 (100 = 10.0 Hz) |
| 3 | R/W | PI setpoint / analog out | 0..4096 = 0..100.0 % |
| 4 | R/W | Ramp time | seconds × 100 (250 = 2.5 s), only when P-12 = 4 |
| 6 | R | Status + error code | low byte = status, high byte = trip code |
| 7 | R | Output frequency | Hz × 10 |
| 8 | R | Output current | A × 10 |
| 11 | R | Digital input status | bit 0 = DI1 |
| 20 | R | Analog input 1 | 0..1000 = 0..100 % |
| 21 | R | Analog input 2 | 0..1000 |
| 22 | R | Speed reference value | Hz × 10 |
| **23** | R | **DC bus voltage** | Volts, 0..1000 |
| 24 | R | Heatsink temperature | °C, 0..100 |

Extended block (read only):

| Reg | Function | Scaling |
|---|---|---|
| 2001 | Status word 2 | bitfield, below |
| 2002 | Motor speed | Hz, 1 dp |
| 2003 | Motor current | A, 1 dp |
| **2004** | **Motor power** | kW, 1 dp |
| 2005 | IO status word | bitfield |
| **2006** | **Motor torque** | ±200.0 % |
| 2007 | DC bus voltage | 0–1000 V |
| 2008 | Heatsink temperature | °C |
| 2009 / 2010 | Analog in 1 / 2 | 0..4096, 12-bit |
| 2011 | Analog output | 0..100.0 % |
| 2012 | PI output | 0..100.0 % |
| 2013 | Internal temperature | °C |
| **2014** | **Motor output voltage** | 0–500 V |
| 2015 | IP66 pot input | 0..4096 |
| 2016 | Trip code | see fault table |

**Register 2006 (torque) matters.** `DESIGN_NOTES.md` concluded torque would be worth
about ±4.5 gpm against current's ±11, because it carries no magnetising offset, and
flagged finding it as the highest-leverage open item on flow estimation. It is here.

### Control word (register 1)

| Bit | Meaning |
|---|---|
| 0 | 0 = stop, 1 = run |
| 1 | fast stop, uses ramp 2 (P-24) |
| 2 | fault reset — **clear it again afterwards** or you get unexpected resets |
| 3 | coast stop |
| 8 | relay control |
| 9 | digital output control (1 = off) |

Priority is bit 3 > bit 1 > bit 0, so `0x0009` coasts rather than runs.

**Run/stop over Modbus only works when P-31 = 0 or 1.** Otherwise start/stop stays with
the control terminals. Fault reset (bit 2) works regardless, provided P-12 = 3 or 4.

### Status word (register 6, low byte)

bit 0 running · bit 1 tripped · bit 5 standby · bit 6 ready
(ready = not tripped, hardware enable present, no mains loss).
High byte carries the trip code.

## Parameters over Modbus

`register = 128 + parameter number`, documented for **P-04 … P-60 only**.
So P-15 → 143, P-36 → 164. All user parameters are readable and writable this way
(**function 06 to write**).

The manual notes *"internal scaling is used on some parameters"* without saying which —
so read a parameter back after writing it rather than assuming the units.

### Parameters that decide whether we can drive the thing

| Par | Meaning | Default | Notes |
|---|---|---|---|
| **P-12** | Primary command source | 0 | **3** = Modbus, internal ramps · **4** = Modbus, ramps via register 4 |
| **P-01** | Max speed limit | 50.0 (60.0) | Hz **or RPM** — see P-10 |
| **P-02** | Min speed limit | **20.0** | Hz or RPM. A floor the drive applies to *our* reference |
| P-03 / P-04 | Accel / decel ramp | 5.0 s | 0 → P-09 in seconds |
| P-07/08/09 | Motor V / A / Hz | — | nameplate |
| **P-10** | Motor rated speed | **0** | 0 → everything in **Hz**. Non-zero → everything in **RPM**, and slip compensation turns on |
| P-14 | Extended menu access | 0 | 101 = extended, 201 = advanced |
| P-31 | Keypad/Modbus start mode | 1 | **must be 0 or 1** for Modbus run/stop |
| P-36 | Comms config | — | idx1 address, idx2 baud, idx3 watchdog ms (`t` suffix = trip, `r` = coast) |

> **P-12 = 3 or 4 still needs the hardware enable.** The manual is explicit: *"When
> P-12 = 1, 2, 3, 4, 7, 8 or 9, an enable signal must still be provided at the control
> terminals, digital input 1."* No link on terminals 1–2, no motor, whatever Modbus says.

> **P-02 is why a low speed reference gets ignored.** The drive clamps our register-2
> value up to its own minimum. Default is 20.0 Hz. Commanding 4.5 Hz into a drive with
> P-02 at 18 gets you 18.

## Trip codes seen here

| Code | Display | Meaning |
|---|---|---|
| 04 | `I.t-trP` | motor thermal overload, >100 % of P-08 sustained |
| 09 | `U-t` | under temperature |
| 11 | `E-triP` | external trip on DI3 |
| 18 | `4-20 F` | 4–20 mA signal lost — the transducer-fault trip P-16 arms |
| **50** | `SC-F01` | **Modbus comms loss** — no valid telegram inside the P-36 idx3 window |
| 51 | `SC-F02` | CAN comms loss |

**Trip 50 is the one to expect on this bench.** Every reflash silences Modbus for ~30 s
and the drive trips exactly as the watchdog is meant to make it. Clear it with `reset`
on the console or Fault reset on the Home screen.

After an over-current or overload trip (3, 4, 5, 15) the drive **refuses to reset**
immediately — 2 s, then 4, 8, 16, 32, 64 s on repeats.

## Corrections to earlier guesses

Recorded so nobody re-derives them:

- **Registers 129 / 130 are NOT P-01 / P-02.** They read 3600 and 3480 on this drive
  while the keypad showed P-02 = 18.0. The documented formula starts at **P-04**, so
  anything below register 132 is undocumented and the observed values do not correspond.
  P-01/P-02 must be read from the keypad, or found another way.
- **Registers 9/10/11/15 were being logged as unknown "voltage candidates".** Motor
  output voltage is **2014** and DC bus is **23** / **2007**. The guessing was unnecessary.
- **Function 16 was being used for parameter writes.** It only works on registers 1–4.
- **P-12 = 3 was never the fault.** 3 and 4 are both valid Modbus control modes.

## Source

Invertek Optidrive ODE-3 IP66 Outdoor Rated User Guide, version 1.26, firmware 3.11 —
§6 Parameters, §8 Modbus RTU Communications, §9.2 Additional Information, §11 Troubleshooting.
The full register map is stated to be available from an Invertek sales partner; the
tables above are what the public guide contains.
