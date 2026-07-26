# Dynamic Cube Pool with Console Reassignment

**Status:** Proposed design; no implementation
**Date:** 2026-07-26
**Owners:** `cube-pn5180` firmware, `cubes` game server, `pi-deploy`

## Decision

Make a cube's **logical slot** a runtime value the server assigns, instead of a
value compiled into firmware. A pre-commissioned spare takes over a failed
cube's slot through one admin-console action and a reboot — no reflashing.

Nothing else changes. Static IPs, the `cube/{slot}/...` topic protocol,
sleep/wake, NFC, and display all stay as they are today. There are no epochs,
leases, manifests, digests, quarantine states, power intents, custom time
authority, or DHCP migration.

## The one idea

Today a cube's identity comes from a compile-time MAC→`cube_id` table
(`getCubeIpOctet`, `src/main.cpp`). That single `cube_id` is overloaded to drive
the IP octet (`cube_id + 20`), the MQTT topics (`cube/{id}/...`), the display
rotation, and it is bundled with genuine hardware traits (RGB order, board
pins). To make a physical cube "become cube 4" you edit that table and reflash.

We un-bundle two things that were tangled together:

| Property | Keyed by | Source | Changes on replacement? |
|---|---|---|---|
| IP octet, RGB order, board pins | physical cube (MAC) | compiled MAC table, unchanged | No — permanent per cube |
| Logical slot (topics + rotation) | server assignment | retained `cube/assign/{MAC}` | Yes — the only dynamic value |
| NFC tag → slot | server maps `MAC→tag` × `slot→MAC` | commissioning + assignment | Follows the assignment |

The physical facts stay compiled and keyed to the MAC. Only the logical slot
moves to the server.

## Why IP stays fixed per physical cube

The static-IP scheme exists for fast reconnect after a mid-game power bounce and
to keep the router's DHCP server off the critical path. We keep it.

If IP instead followed the slot, a returning "dead" cube and its replacement
would both claim the same address and fight on the network — an ambiguous IP
conflict. Fixing IP to the physical cube removes that failure entirely: the old
cube returns on its own address, finds it holds no slot assignment, and sits
idle. It also removes any boot-time chicken-and-egg — a cube derives its IP from
its own MAC before it needs the network, exactly as today, so it can then read
its slot assignment over that connection with no DHCP and no self-reboot.

## Field workflow

1. Stop the game after a cube fails.
2. Power off or remove the failed cube.
3. Put a compatible, pre-commissioned spare in its place and power it on.
4. On the admin console, assign the failed cube's slot to the spare's MAC.
5. Reboot the cubes.
6. Each cube reads its assignment at boot and takes its slot. The server does
   not start a round until every assigned slot has a present cube.

No flashing, no per-cube IP surgery, no release command.

## Server (`cubes` + admin console)

- Keep a persisted, mutable `slot → MAC` map on the Pi, seeded from today's
  assignments. It is a plain dict, not an epoch history.
- Console shows every known cube: MAC, present/absent, and current slot (or
  `unassigned`). One action — **assign slot N → MAC** — atomically clears slot N
  from whoever held it, so one slot maps to exactly one MAC. A returning cube can
  never re-grab a slot that has been reassigned.
- Publish each cube's slot as a retained `cube/assign/{MAC}` record. Removing a
  cube from all slots publishes an explicit `unassigned`.
- **Game-start presence gate:** do not start a round until all 6 (single-player)
  or 12 (two-player) assigned slots report a present cube. Because the game is
  stopped during a swap, this is all the safety the topology needs — no partial
  neighbor graph can reach scoring when no round is running. This replaces the
  previous design's atomic startup topology barrier.
- Resolve NFC neighbor identity server-side (see below).
- Everything else in the `cube/{slot}/...` protocol is unchanged.

### Assignment record

Topic (retained): `cube/assign/{MAC}`

```json
{ "protocol": 1, "slot": 4 }
```

or, when the cube holds no slot:

```json
{ "protocol": 1, "slot": null }
```

`MAC` is the uppercase MAC without colons. The record is small and bounded;
firmware subscribes only to its own `cube/assign/{own_MAC}`.

## Firmware (`cube-pn5180`)

Boot order works with no chicken-and-egg because IP no longer depends on slot:

1. Look up own MAC in the compiled table → IP octet + hardware traits →
   configure static IP → connect to MQTT. **Unchanged from today.**
2. Read retained `cube/assign/{own_MAC}`.
3. Act on the record:
   - **`{slot: N}`** — set the topic base to `cube/N/...` and rotation from N
     (`N <= 6 ? 2 : 0`, as today), then run normally.
   - **`{slot: null}`** (explicit unassigned) — display `UNASSIGNED`, subscribe
     to no slot topics, publish no neighbor/sensor state. This is the
     returned-old-cube state.
   - **No record at all** — fall back to the compiled `cube_id` as the slot (see
     transitional fallback below).

The slot that used to be a compiled constant is now a value read once at boot.
Applied at boot only — the admin reboots during a swap anyway — so there is no
live IP or topic reconfiguration.

**Transitional fallback:** the "no record at all" case exists only during
rollout, before the server publishes assignments. Falling back to the compiled
`cube_id` lets the assignment-aware firmware ship before the server side is
authoritative without changing behavior. Once the server is authoritative every
cube has a record — an explicit `{slot: null}` is idle, and absence no longer
occurs in the field.

## NFC neighbor identity

A cube reads the tag of its **right neighbor** and publishes it. Today it also
resolves that tag to a cube number on-device via the compiled
`lookupCubeNumberByTag` and publishes `cube/right/{id}`.

In the pool model the tag→slot mapping is dynamic, so resolution moves to the
server:

- The cube keeps publishing the raw neighbor tag on `cube/nfc/{id}` (it already
  does). On-device tag→number resolution and `cube/right/{id}` publication are
  retired for pooled identity.
- The server resolves raw tag → slot using two facts it already holds: a
  `MAC → tag` value recorded once at commissioning, and the live `slot → MAC`
  assignment. Because a spare brings its own tag, its tag must be recorded on the
  console before it is assigned — one data-entry field, not a reflash.

## What this drops from the previous design

Epochs, leases, manifests, SHA-256 digests, quarantine states, power intents,
the Pi-boot-scoped time authority, absolute wake deadlines, the exactly-once
reboot NVS state machine, the atomic startup topology barrier, and the
DHCP/mDNS migration. Sleep/wake is untouched.

## Constraints and non-goals

Constraints:

- A game uses 6 cubes (single-player) or 12 (two-player).
- Small and large cubes are not interchangeable; a spare may only take a slot of
  its own hardware class. The console enforces this on assignment.
- A pre-commissioned spare already runs current firmware and is present in the
  compiled MAC table (for its IP octet and hardware traits) and in the server's
  `MAC → tag` inventory.

Non-goals:

- Replacing a cube without stopping the game.
- Changing a slot assignment from a heartbeat timeout or from sleep. Only an
  explicit console action reassigns.
- Commissioning a brand-new cube without a reflash. Adding a new MAC to the
  compiled table is a planned commissioning event, not field replacement.
- Reworking sleep, the time model, or the network addressing scheme.

## Failure and recovery behavior

| Situation | Result |
|---|---|
| Active cube misses heartbeat | Health reports offline; slot assignment unchanged |
| Active cube sleeps | Unchanged sleep behavior; slot assignment unchanged |
| Active cube fails mid-game | Game stops until admin reassigns and reboots |
| Failed cube returns after reassignment | It reads `unassigned`, sits idle; no IP conflict |
| Spare assigned to a taken slot | Server clears the slot from the prior MAC first |
| Wrong-size spare assigned | Console rejects: hardware class must match |
| Not all assigned slots present | Round does not start; console shows which are missing |
| Server restart | Reload the `slot → MAC` map from disk; assignments unchanged |
| New firmware, no assignment record yet | Falls back to compiled `cube_id`; behavior preserved |

## Implementation areas

### `cube-pn5180`

- Read logical slot from retained `cube/assign/{MAC}` at boot instead of the
  compiled `cube_id`; keep the compiled table as the source of IP octet and
  hardware traits.
- Subscribe to `cube/assign/{own_MAC}`; add an `UNASSIGNED` idle state.
- Stop resolving tag→number on-device and stop publishing `cube/right/{id}` for
  pooled identity; keep publishing the raw tag on `cube/nfc/{id}`.
- Keep static IP, sleep/wake, display, and game handling as-is.

### `cubes`

- Add a persisted `slot → MAC` map and a `MAC → tag` inventory.
- Publish retained `cube/assign/{MAC}` records; clear a slot from the prior
  holder on reassignment.
- Resolve NFC neighbor tags to slots server-side from those two maps; stop
  consuming `cube/right/{id}` as authoritative.
- Add the game-start presence gate.
- Never change a slot assignment from heartbeat expiry or sleep.

### `pi-deploy`

- Persist the `slot → MAC` map and `MAC → tag` inventory across restarts.
- Add a console `assign-slot` action (and a `swap` shortcut: assign a failed
  cube's slot to a spare and clear the old holder), with hardware-class checks.

## Test plan

### Server unit tests

- Assigning a taken slot clears it from the prior MAC (one MAC per slot).
- Heartbeat expiry and sleep never change an assignment.
- A returning cube whose slot was reassigned resolves to `unassigned`.
- Wrong-hardware-class assignment is rejected.
- A round does not start until every assigned slot is present.
- Server restart reloads the same `slot → MAC` map and re-publishes assignments.
- Raw neighbor tag resolves to the correct slot through `MAC→tag` × `slot→MAC`,
  including after a spare (new tag) is assigned.

### Firmware-native tests

- A cube uses the assigned slot for topics and rotation, not the compiled
  `cube_id`.
- No `cube/assign` record falls back to the compiled `cube_id`.
- An `unassigned` record produces the idle state: no slot subscriptions, no
  neighbor/sensor publication.
- IP octet and hardware traits come from the compiled MAC table regardless of
  slot.
- The cube publishes the raw neighbor tag and no longer publishes
  `cube/right/{id}` for pooled identity.

### Broker / hardware acceptance

- Replace one cube in a 6-cube game via the console and a reboot; verify the
  spare plays the failed slot and forms words on both left and right edges.
- Power the failed cube back on; verify it stays idle with no IP conflict.
- Repeat for a 12-cube game across both player sets.
- Verify small↔small and large↔large replacement; confirm cross-size is
  rejected.
- Leave cubes idle past the sleep threshold; verify no assignment change.

## Rollout

1. Add the server `slot → MAC` map, `MAC → tag` inventory, and retained
   `cube/assign` publishes, seeded to match today — no behavior change.
2. Ship firmware that reads its slot from the assignment (compiled `cube_id`
   fallback), keeping IP and hardware from the compiled table.
3. Add the console assign/swap action and the game-start presence gate.
4. Move neighbor tag resolution to the server; retire compiled
   `lookupCubeNumberByTag` and `cube/right/{id}` for pooled identity.

Steps 1–2 preserve behavior, so there is no flag day. Mixed firmware is safe
until step 4, at which point every field cube must be on the assignment-aware
build.

## Open decisions before implementation

1. Where do the `slot → MAC` map and `MAC → tag` inventory live on the Pi?
2. How does the console capture a spare's tag at commissioning?
3. Confirm hardware-class metadata (small/large) per commissioned cube.
4. Confirm which display state must be re-sent after a slot change (most
   `cube/{slot}/...` display topics are retained and arrive on subscribe).
