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
| IP octet, RGB order, board pins | physical cube (MAC) | compiled MAC table | No — permanent per cube |
| Logical slot (topics + rotation) | server assignment | retained `cube/assign/{MAC}` | Yes — the only dynamic value |
| NFC tag → slot | server maps `MAC→tag` × `slot→MAC` | commissioning + assignment | Follows the assignment |

The physical facts stay compiled and keyed to the MAC. Only the logical slot
moves to the server.

## Why IP stays fixed per physical cube — and the table change it requires

The static-IP scheme exists for fast reconnect after a mid-game power bounce and
to keep the router's DHCP server off the critical path. We keep it.

If IP instead followed the slot, a returning "dead" cube and its replacement
would both claim the same address and fight on the network — an ambiguous IP
conflict. Fixing IP to the physical cube removes that failure: the old cube
returns on its own address, finds it holds no slot assignment, and sits idle. It
also removes any boot-time chicken-and-egg — a cube derives its IP from its own
MAC before it needs the network, so it can read its slot assignment over that
connection with no DHCP and no self-reboot.

**This requires a change to `CubeMacEntry`.** Today the IP octet is
`cube_id + 20` and the production table deliberately gives each backup the *same*
`cube_id` as its primary (`CC:DB:A7:9F:C2:84` and spare `80:F3:DA:54:53:B8` are
both `cube_id` 1, so both compute `192.168.8.21`). Deriving the IP from
`cube_id` therefore does **not** give a unique address per physical cube — a
returning primary beside its active backup produces exactly the conflict above.
The fix: add a distinct `ip_octet` field to `CubeMacEntry`, unique per
commissioned MAC and independent of the legacy `cube_id` (which becomes only a
rollout-time default slot). Commissioning validates one octet per MAC.

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

- Keep a persisted `slot → MAC` map on the Pi with a single monotonic
  `revision` integer, seeded from today's assignments. The persisted map on disk
  — not any MQTT record — is the source of truth. It is a plain versioned dict,
  not an epoch history.
- Console shows every known cube: MAC, present/absent, and current slot (or
  `unassigned`). One action — **assign slot N → MAC** — clears slot N from whoever
  held it, so one slot maps to exactly one MAC. A returning cube can never
  re-grab a reassigned slot.
- **Durable, recoverable swap** (one logical transaction over two MQTT writes):
  1. Persist the new map with an incremented `revision` to disk first.
  2. Publish retained `cube/assign/{old_MAC}` = `{revision, slot: null}`, then
     retained `cube/assign/{new_MAC}` = `{revision, slot: N}`.
  A crash or disconnect between the two publishes is safe: on restart the server
  reloads the persisted map and idempotently republishes every assignment record
  at the current `revision`, converging both halves. The `revision` lets clients
  and the server reject a stale half.
- **Game-start gate proves the *assigned MAC* is present, at the current
  revision.** For each required slot N, the gate requires that the MAC the map
  assigns to slot N has fresh MAC-scoped presence reporting `applied_slot = N`
  and `applied_revision = revision`. Because presence is keyed by MAC (not slot),
  a stale old holder — or a spare that has not yet adopted the new assignment —
  cannot satisfy the gate. Since the game is stopped during a swap, this is all
  the safety the topology needs: no partial neighbor graph reaches scoring when
  no round is running. This replaces the previous design's atomic topology
  barrier.
- Resolve NFC neighbor identity server-side (see below).
- The `cube/{slot}/...` game/display protocol is unchanged; the additions are
  the MAC-scoped assignment, presence, and authority records below.

### Records (all MAC-scoped)

`MAC` is the uppercase MAC without colons. Every record is small and bounded;
firmware subscribes only to its own MAC.

**Authority marker** — retained `cube/roster/authoritative`:

```json
{ "protocol": 1, "authoritative": true, "revision": 7 }
```

Set once the server owns the assignment map. It is the switch that ends the
rollout fallback (see firmware). Its absence means "pre-cutover, fallback
allowed"; its presence means "assignments are authoritative — fail closed."

**Assignment** — retained `cube/assign/{MAC}`:

```json
{ "protocol": 1, "revision": 7, "slot": 4 }
```

or, when the cube holds no slot, `"slot": null`. `revision` matches the map
revision that produced it, so a client or the server can reject a stale record
from an interrupted swap.

**Presence** — retained `cube/device/{MAC}/presence`, with a retained offline
last will:

```json
{ "protocol": 1, "online": true, "boot_id": "random-per-boot",
  "applied_slot": 4, "applied_revision": 7 }
```

The cube publishes this after it has adopted an assignment; `applied_slot` and
`applied_revision` are what the cube actually took, letting the game-start gate
confirm the *assigned MAC* holds the slot at the current revision. This is one
retained record per cube — not a heartbeat, lease, or ack subsystem.

## Firmware (`cube-pn5180`)

Boot order works with no chicken-and-egg because IP no longer depends on slot:

1. Look up own MAC in the compiled table → **`ip_octet`** (new unique field) +
   hardware traits → configure static IP → connect to MQTT.
2. Read retained `cube/roster/authoritative` and `cube/assign/{own_MAC}`.
3. Resolve the slot (fail closed once authoritative):
   - **`{slot: N}`** — set the topic base to `cube/N/...` and rotation from N
     (`N <= 6 ? 2 : 0`, as today), persist `{slot: N, revision}` to NVS, publish
     MAC-scoped presence with `applied_slot`/`applied_revision`, then run.
   - **`{slot: null}`** — display `UNASSIGNED`, subscribe to no slot topics,
     publish no neighbor/sensor state, clear the NVS slot. Returned-old-cube
     state.
   - **Missing or malformed record while `cube/roster/authoritative` is set** —
     fail closed to `UNASSIGNED` and wait for the server. Do **not** fall back to
     the compiled slot.
   - **Missing record and no authority marker (pre-cutover only)** — fall back to
     the compiled `cube_id` as the slot (transitional).

Fallback keys off the explicit authority marker, not the mere absence of a
record. This closes the ambiguity where delayed retained delivery, an empty or
restarted broker, or a server that has not yet republished its map would
otherwise let a returned old cube resume its compiled slot and collide.

**The maintenance (timer-wake) path is not left untouched.** Today every
deep-sleep reset runs `getCubeIpOctet()` (which resets `cube_identifier` to the
compiled `cube_id`) and `handleWakeUp()` builds the keepalive client ID,
`cube/{id}/status`, and `cube/{id}/auto_sleep` from that compiled value before
full MQTT setup — so a reassigned spare would revert to its old logical identity
on every maintenance wake. The fix:

- Keepalive/status/auto-sleep identity becomes **MAC-scoped**
  (`cube/device/{MAC}/...`), not slot-scoped, so a maintenance wake never
  publishes under a compiled or stale slot.
- Before any slot-scoped publish, the maintenance client reads retained
  `cube/assign/{MAC}` and validates it against the NVS `{slot, revision}`; on
  mismatch or a newer revision it defers to a full-client start rather than
  acting on the old slot. Sleep timing itself (10-min inactivity, timer wake) is
  unchanged.

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
DHCP/mDNS migration.

What it keeps small but does add, for correctness against the current firmware:
a unique per-MAC `ip_octet`; a single monotonic assignment `revision`;
MAC-scoped presence (one retained record per cube); an explicit authority
marker; and MAC-scoped keepalive on the timer-wake path. **Sleep timing is
unchanged, but the sleep/keepalive path is not left untouched** — its client
identity moves from the compiled slot to the MAC.

## Constraints and non-goals

Constraints:

- A game uses 6 cubes (single-player) or 12 (two-player).
- Small and large cubes are not interchangeable; a spare may only take a slot of
  its own hardware class. The console enforces this on assignment.
- A pre-commissioned spare already runs current firmware and is present in the
  compiled MAC table with a unique `ip_octet` and its hardware traits, and in the
  server's `MAC → tag` inventory.

Non-goals:

- Replacing a cube without stopping the game.
- Changing a slot assignment from a heartbeat timeout or from sleep. Only an
  explicit console action reassigns.
- Commissioning a brand-new cube without a reflash. Adding a new MAC (with its
  octet) to the compiled table is a planned commissioning event, not field
  replacement.
- Reworking sleep timing, the time model, or the static-IP addressing scheme.

## Failure and recovery behavior

| Situation | Result |
|---|---|
| Active cube misses heartbeat | Health reports offline; slot assignment unchanged |
| Active cube sleeps | Unchanged sleep behavior; slot assignment unchanged |
| Active cube fails mid-game | Game stops until admin reassigns and reboots |
| Failed cube returns after reassignment | Reads `unassigned`, sits idle; its own `ip_octet` means no IP conflict |
| Spare assigned to a taken slot | Server clears the slot from the prior MAC first, at a new revision |
| Crash between the two swap publishes | Restart reloads the persisted map and republishes both records at the current revision |
| Wrong-size spare assigned | Console rejects: hardware class must match |
| Stale/old holder tries to satisfy the gate | Gate checks the assigned MAC at the current revision; stale presence is rejected |
| Not all assigned MACs present at revision | Round does not start; console shows which are missing |
| Server restart | Reload the versioned `slot → MAC` map from disk; republish assignments |
| Missing assignment while authority marker set | Firmware fails closed to `UNASSIGNED`; no compiled-slot fallback |
| Reassigned spare wakes from deep sleep | Maintenance path is MAC-scoped and revalidates assignment; never reverts to old slot |

## Implementation areas

### `cube-pn5180`

- Add a unique `ip_octet` field to `CubeMacEntry`, independent of `cube_id`;
  derive the static IP from it. Validate one octet per commissioned MAC.
- Read logical slot from retained `cube/assign/{MAC}` at boot instead of the
  compiled `cube_id`; persist the applied `{slot, revision}` in NVS.
- Subscribe to `cube/assign/{own_MAC}` and `cube/roster/authoritative`; add an
  `UNASSIGNED` idle state; fail closed (not to the compiled slot) when authority
  is set and the record is missing or malformed.
- Publish retained MAC-scoped presence (`cube/device/{MAC}/presence`) with
  `applied_slot`/`applied_revision` and an offline last will.
- Move the timer-wake keepalive/status/auto-sleep identity to MAC-scoped topics;
  revalidate the retained assignment against NVS before any slot-scoped publish.
- Stop resolving tag→number on-device and stop publishing `cube/right/{id}` for
  pooled identity; keep publishing the raw tag on `cube/nfc/{id}`.
- Keep sleep timing, display, and game handling as-is.

### `cubes`

- Add a persisted, versioned `slot → MAC` map (single monotonic `revision`) and
  a `MAC → tag` inventory; the on-disk map is the source of truth.
- Perform swaps as a durable transaction: persist the incremented revision,
  then publish `unassigned` for the old MAC and the new slot for the spare; on
  restart, reload and idempotently republish all records at the current revision.
- Publish the retained `cube/roster/authoritative` marker at cutover.
- Game-start gate: require every required slot's assigned MAC to report
  MAC-scoped presence with matching `applied_slot`/`applied_revision`.
- Resolve NFC neighbor tags to slots server-side from `MAC→tag` × `slot→MAC`;
  stop consuming `cube/right/{id}` as authoritative.
- Never change a slot assignment from heartbeat expiry or sleep.

### `pi-deploy`

- Persist the versioned `slot → MAC` map and `MAC → tag` inventory across
  restarts.
- Add a console `assign-slot` action (and a `swap` shortcut: assign a failed
  cube's slot to a spare and clear the old holder), with hardware-class checks.
- Assign and record a unique `ip_octet` per commissioned MAC.

## Test plan

### Server unit tests

- Assigning a taken slot clears it from the prior MAC (one MAC per slot) and
  increments the revision.
- Heartbeat expiry and sleep never change an assignment.
- A returning cube whose slot was reassigned resolves to `unassigned`.
- Wrong-hardware-class assignment is rejected.
- The gate opens only when every required slot's *assigned MAC* reports presence
  at the current revision; a stale/old holder's presence never satisfies it.
- A crash after persisting the map but before (or between) the publishes
  recovers on restart: the map reloads and both records republish at the current
  revision (no double-claim, no missing half).
- Server restart reloads the same versioned map and re-publishes assignments.
- Raw neighbor tag resolves to the correct slot through `MAC→tag` × `slot→MAC`,
  including after a spare (new tag) is assigned.

### Firmware-native tests

- A cube uses the assigned slot for topics and rotation, not the compiled
  `cube_id`, and persists `{slot, revision}` to NVS.
- With the authority marker set, a missing or malformed assignment fails closed
  to `UNASSIGNED` — no compiled-slot fallback.
- Without the authority marker (pre-cutover), a missing record falls back to the
  compiled `cube_id`.
- An `unassigned` record produces the idle state: no slot subscriptions, no
  neighbor/sensor publication.
- The static IP comes from the per-MAC `ip_octet`, unique per physical cube,
  regardless of slot.
- After reassignment, a timer wake uses MAC-scoped keepalive topics and does not
  publish under the old slot; it revalidates the assignment before slot-scoped
  publishes.
- The cube publishes MAC-scoped presence with `applied_slot`/`applied_revision`,
  and the raw neighbor tag on `cube/nfc/{id}`; it no longer publishes
  `cube/right/{id}` for pooled identity.

### Broker / hardware acceptance

- Replace one cube in a 6-cube game via the console and a reboot; verify the
  spare plays the failed slot and forms words on both left and right edges.
- Power the failed primary and its active backup on **simultaneously**; verify
  each keeps its own IP (no `.21` conflict) and only the assigned MAC plays.
- Kill the server between the two swap publishes, restart it, and verify the
  swap converges without a double-claimed slot.
- Reassign a spare, let it enter deep sleep, and verify a maintenance wake never
  re-publishes under its old slot.
- Repeat for a 12-cube game across both player sets.
- Verify small↔small and large↔large replacement; confirm cross-size is
  rejected.
- Leave cubes idle past the sleep threshold; verify no assignment change.

## Rollout

1. Assign a unique `ip_octet` per commissioned MAC in `CubeMacEntry`; ship it so
   every physical cube has a distinct address (no shared-`cube_id` collisions).
2. Add the server versioned `slot → MAC` map, `MAC → tag` inventory, and retained
   `cube/assign` publishes, seeded to match today — no behavior change. The
   authority marker stays off, so firmware fallback still applies.
3. Ship firmware that reads its slot from the assignment (compiled `cube_id`
   fallback while the authority marker is off), persists `{slot, revision}` to
   NVS, publishes MAC-scoped presence, and uses MAC-scoped keepalive on wake.
4. Add the console assign/swap action and the MAC-verified game-start gate, then
   publish `cube/roster/authoritative` — after which firmware fails closed
   instead of falling back.
5. Move neighbor tag resolution to the server; retire compiled
   `lookupCubeNumberByTag` and `cube/right/{id}` for pooled identity.

Steps 1–3 preserve behavior, so there is no flag day. Mixed firmware is safe
until step 4 (authority cutover), at which point every field cube must be on the
assignment-aware build.

## Open decisions before implementation

1. Where do the versioned `slot → MAC` map and `MAC → tag` inventory live on the
   Pi, and what is the durable-write mechanism (single-file atomic replace)?
2. How does the console capture a spare's tag and unique `ip_octet` at
   commissioning?
3. Confirm hardware-class metadata (small/large) per commissioned cube.
4. Confirm which display state must be re-sent after a slot change (most
   `cube/{slot}/...` display topics are retained and arrive on subscribe).
5. Confirm the NVS namespace/keys for the applied `{slot, revision}` and their
   interaction with existing sleep-state persistence.
