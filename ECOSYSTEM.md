# Ecosystem — more than one of these on a site

How several ESP32 nodes (pump, tank level, flow, dosing, gateway) live together.
Written before there is a second node, because the decisions that are expensive
to reverse are the addressing and the failure model, and both get baked in the
moment the second board is commissioned.

Nothing here is built yet except the identity and Wi-Fi groundwork in `net.h`
and the Wi-Fi screen. This is the plan those were built to fit.

## The rule everything else is derived from

> **A node performs its function with the network completely down.**

PumpSaver holds pressure with no AP, no station, no peer, and no gateway. That
is not a nice-to-have — it is the same reasoning as `gEnable` not being
persisted and `xPsiValid` freezing the loop. A control decision that depends on
a radio is a control decision that fails at 2 a.m. in a way nobody can diagnose.

Four consequences, and they are the contract every future node signs:

1. **Every node is standalone.** Networking carries information *about* control,
   never control itself.
2. **Cross-node data is advisory and carries its age.** A consumer must have a
   defined, safe behaviour when a value goes stale — not "use the last one
   forever".
3. **Absence may stop something; absence may never start something.** A node
   that stops pumping because the tank node went quiet is fine. A node that
   starts pumping because a packet did not arrive is not.
4. **One writer per setpoint.** Exactly like Mode 2: PumpSaver either owns the
   loop or it does not, never both. If a gateway can write a setpoint, the
   local UI must show that it is being written from elsewhere.

## Topology — the options, and the choice

| | How | Verdict |
|---|---|---|
| **A. Star on one node's AP** | one ESP is the AP, the rest join it | **No.** softAP tops out around 4–10 clients, the AP node becomes the single point of failure for every link, and its own web UI competes with the traffic it is routing. Fine for two boards on a bench. |
| **B. Small AP/router in the panel** | every node is a station with a static address | **Chosen.** One cheap box, ordinary IP networking, every node reachable from one laptop, and standard tools work. Its failure removes *coordination* only — rule 1 means every node keeps running. |
| **C. ESP-NOW** | connectionless, MAC-addressed, ~250 B frames, single-digit ms | **As a sideband, not as the network.** Cannot serve a web page, has no routing, and is pinned to the station's channel. Right answer for one fast interlock; wrong answer for the whole system. |
| **D. ESP-WIFI-MESH / painlessMesh** | self-healing tree | **No.** Heavy, awkward alongside the async web server, and effectively undebuggable from a phone in a wet plant room. |

**B as the transport, C later as an optional fast sideband, and every node keeps
its own AP permanently.**

That last part is the important one. The AP is not a fallback for commissioning
— it is how a technician reaches a specific board while standing in front of it,
whatever the site network is doing. `net.h` brings the AP up even when a station
is configured, and a wrong SSID can therefore never strand a board.

### The one-radio tax

The ESP32 has a single radio. In AP+STA the softAP is *forced* onto whatever
channel the station associates on, so anyone connected to the board's own AP is
bumped and has to reconnect when it joins the router. This is normal and is
noted on the Wi-Fi screen. It is also a reason not to run a scan casually — the
sweep drops AP clients for a few seconds.

## Addressing

```
10.10.42.0/24
  .1  ..  .250    one node, last octet == node id
  .254            the panel AP / router
```

- **Why 10.10.42:** unlikely to collide with a site LAN, which is nearly always
  `192.168.x` or `10.0.x`. If it does collide, change the block once, at
  commissioning, everywhere.
- **Why id == last octet:** "node 3" and `10.10.42.3` are the same fact. A
  mapping table that lives only in someone's head is a mapping table that is
  wrong within a year.
- **Static, not DHCP.** A pump skid should come back after a power cut without
  needing a DHCP server to have booted first.

Each node's own AP stays on `192.168.4.1` (the ESP default). That is not a
collision — you are only ever joined to one node's AP at a time.

### Names

mDNS is the primary handle: `pump-1.local`, `tank-1.local`. It survives an
address change and it is what a human will actually type. The static address is
the fallback, because mDNS is unreliable on exactly the devices field staff
carry — patchy on Android, and dependent on the Bonjour service on Windows.

**Publish both. Rely on neither alone.** The node name is sanitised to
`[a-z0-9-]` on save (`sanitizeNode` in the `.ino`) because it becomes a
hostname, and "Pump #1" resolves for nobody.

## Discovery

Every node already advertises `_http._tcp` with TXT records:

```
role=pump   node=pump-1   id=1
```

So a gateway enumerates the site by browsing mDNS rather than carrying a
hard-coded address list. A node that has been replaced announces itself; a node
that has died stops appearing. Add the address list as a manual override for
when mDNS is being unhelpful — never as the only mechanism.

## Data plane — pull, not push

A node with `role=gateway` polls each node's `/status` on a timer.

Pull is chosen over push for one reason: **every link in the system is then a URL
a human can open.** If the gateway shows the wrong tank level, you open
`http://tank-1.local/status` on your phone and you immediately know which side
of the link is lying. With push, a node that has stopped reporting is invisible
until someone thinks to read the hub's log.

Cost of pull: latency is the poll interval, and the gateway does N requests per
cycle. At 1 Hz across five nodes that is nothing.

Each node's `/status` already carries `up` (seconds since boot), which is what
lets a consumer age a value and notice a silent reboot.

**MQTT** is the obvious "proper" answer and is deliberately deferred: it needs a
broker box, and it makes every node's behaviour depend on that box being alive —
which is rule 1 again, in a nicer wrapper. Revisit if a broker already exists on
site for other reasons.

**ESP-NOW** earns its place only where the poll interval is too slow to be safe —
a hard interlock like "tank empty, stop now". If that case appears, add it as a
*second, independent* path alongside the HTTP one, not as a replacement.

## Roles

| Role | Function | Talks to |
|---|---|---|
| `pump` | this firmware — holds header pressure, 1–2 drives | — |
| `tank` | level, and a dry-run / overflow interlock | pump (advisory), gateway |
| `flow` | the flow meter DESIGN_NOTES argues for | pump (sleep decision), gateway |
| `dosing` | chemical feed paced off flow | flow, gateway |
| `gateway` | polls everything, one screen, logging, offsite link | all |

The `flow` role is the interesting one: `FB_PumpControl` already takes
`rFlowGPM` / `xFlowValid`, and DESIGN_NOTES shows speed cannot resolve 2 gpm
from 20 gpm near shutoff. A networked flow node would feed a real control input
— which means it is the first case that bumps into rule 2 hard. **A stale flow
reading must set `flowValid = false`, not hold the last number.** The block
already handles `flowValid` going false; that path must be exercised on the
bench before a flow node is trusted.

## Build order

1. **Done** — identity (name / role / id), AP + station config, static IP, mDNS
   with TXT records, Wi-Fi screen. `v0.6.0`.
2. Station mode proven against a real router: reconnect after the router
   reboots, and after the *node* reboots with the router absent.
3. A second board flashed as `role=tank` with a stub `/status`, purely to prove
   discovery and the standalone rule.
4. A `gateway` build: mDNS browse, poll every node, one page listing them, with
   an explicit age on every value.
5. Only then, a real cross-node input — flow into the sleep decision — with the
   stale-data path tested first.

## Open questions

| # | Question | Blocks |
|---|---|---|
| E1 | Panel AP hardware — an industrial unit, or a consumer router that has to survive the environment? | step 2 |
| E2 | Does the site have a network we are expected to join, or is this an island? Changes whether `10.10.42/24` is ours to pick. | addressing |
| E3 | Does anything need offsite/cellular reach, or is the gateway local only? | gateway scope |
| E4 | Is there an existing MQTT broker or SCADA on any of these sites? | pull vs MQTT |
| E5 | 81 % of the app partition is used at `v0.6.0`. OTA needs two app slots — does the partition scheme need changing before more nodes exist? | OTA, step 4 |

On E5: the board has 16 MB of flash and the sketch is currently on a scheme
that offers 1.31 MB to the application. Changing the partition scheme changes
the FQBN, so it is a `sketch.json` + `build.ps1` change and a full rebuild —
cheap now, annoying once five boards are in the field on the old layout.
