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

## Scoping principle

All physical/liveness/observation traffic — MQTT client IDs, presence, NFC
snapshots — is **MAC-scoped and provenance-tagged**. Only server→cube
game/display commands stay slot-scoped (`cube/{slot}/...`). This is what keeps a
returning old holder from aliasing a slot's identity, liveness, or topology, and
it is applied uniformly below.

## Server (`cubes` + admin console)

- Keep a persisted `MAC → {slot, generation}` map on the Pi, seeded from today's
  assignments. The on-disk map — not any MQTT record — is the source of truth.
  Each device carries its **own** `generation` that increments only when that
  device's assignment changes. There is no global revision.
- Console shows every known cube: MAC, present/absent, and current slot (or
  `unassigned`). One action — **assign slot N → MAC** — clears slot N from whoever
  held it, so one slot maps to exactly one MAC. A returning cube can never
  re-grab a reassigned slot.
- **Durable, recoverable swap** — a swap touches exactly the two affected
  devices:
  1. Persist the map to disk first, bumping `generation` for the old MAC (now
     `slot: null`) and the new MAC (now `slot: N`). Unchanged cubes keep their
     slot **and** generation.
  2. Publish retained `cube/assign/{old_MAC}` then `cube/assign/{new_MAC}`, each
     carrying its own new `generation`.
  A crash between the two publishes is safe: on restart the server reloads the
  map and idempotently republishes every device's assignment at its stored
  generation, converging both halves with no double-claim.
- **Game-start gate proves each *assigned MAC* is live at its own generation.**
  For each required slot N → MAC M, the gate requires:
  1. M's current assignment says `slot = N`;
  2. M answers a fresh, non-retained **liveness challenge** (nonce) echoing its
     `boot_id` and `generation = M`'s assigned generation, received within a
     bounded interval;
  3. M has published a fresh post-assignment NFC snapshot for that generation.
  Gating each MAC against *its own* generation means an unchanged cube (whose
  generation did not move) still passes after a one-slot swap — only the swapped
  pair must re-prove. Retained `online: true` is **not** accepted as liveness
  (see presence). Since the game is stopped during a swap, this replaces the
  previous design's atomic topology barrier.
- Resolve NFC neighbor identity server-side, rejecting any observation whose
  sender MAC/generation is not the current assignee (see below).

### Records (all MAC-scoped)

`MAC` is the uppercase MAC without colons. Every record is small and bounded;
firmware subscribes only to its own MAC.

**Authority marker** — retained `cube/roster/authoritative`:

```json
{ "protocol": 1, "authoritative": true }
```

Set once the server owns the assignment map. A cube that observes it **latches
authority in NVS** and never re-enables fallback afterward — absence of the
marker (lost retained state, or booting before the server republishes) can never
downgrade an already-cut-over cube back to compiled-slot fallback. See firmware.

**Assignment** — retained `cube/assign/{MAC}`:

```json
{ "protocol": 1, "generation": 3, "slot": 4 }
```

or, when the cube holds no slot, `"slot": null`. `generation` is this device's
own counter; it lets a client or the server reject a stale record from an
interrupted swap without coupling to other devices.

**Presence** — retained `cube/device/{MAC}/presence`, with a retained offline
last will. It drives the console display and MUST NOT be treated as liveness:

```json
{ "protocol": 1, "state": "online|sleeping|offline",
  "boot_id": "random-per-boot", "applied_slot": 4, "applied_generation": 3 }
```

The deep-sleep path disconnects cleanly (no last will fires), so the cube
**explicitly publishes `state: sleeping` before entering deep sleep**; a stale
retained `online` therefore cannot masquerade as awake.

**Liveness challenge / response** — the gate's actual proof of life, both
non-retained:

```text
cube/device/{MAC}/liveness-request    (server → cube: nonce)
cube/device/{MAC}/liveness-response   (cube → server)
```

```json
{ "protocol": 1, "nonce": "server-nonce", "boot_id": "random-per-boot",
  "generation": 3, "applied_slot": 4 }
```

The cube answers only while fully awake with a matching assignment. The gate
accepts a response only if the nonce, `boot_id` (matching current presence), and
`generation` (matching the device's assigned generation) all agree and it
arrived within a bounded interval. This is a one-shot probe at game-start, not a
continuous heartbeat; a server restart simply re-issues challenges.

## Firmware (`cube-pn5180`)

**MQTT client ID is MAC-derived from the first connection**, for both the full
and maintenance clients, independent of any slot. Today `setMqttClientName()`
uses the compiled/logical slot; because a cube must connect *before* it can read
`cube/assign/{MAC}`, two physical cubes sharing a default slot (or several
unassigned cubes) would present the same client ID and the broker would kick one
off as the other connects. A unique MAC-derived client ID removes that collision
for a returning holder beside its replacement.

Boot order then works with no chicken-and-egg because IP no longer depends on
slot:

1. Look up own MAC in the compiled table → **`ip_octet`** (new unique field) +
   hardware traits → configure static IP → connect to MQTT with the MAC-derived
   client ID and install the MAC-scoped presence last will.
2. Read retained `cube/roster/authoritative` and `cube/assign/{own_MAC}`. If the
   marker is set, **latch authority in NVS** (never cleared by later absence).
3. Resolve the slot (fail closed once authority is latched):
   - **`{slot: N}`** — set the topic base to `cube/N/...` and rotation from N
     (`N <= 6 ? 2 : 0`, as today), persist `{slot: N, generation}` to NVS,
     publish MAC-scoped presence, answer liveness challenges, then run.
   - **`{slot: null}`** — display `UNASSIGNED`, subscribe to no slot topics,
     publish no neighbor/sensor state, clear the NVS slot. Returned-old-cube
     state.
   - **Missing or malformed record while authority is latched** — fail closed to
     `UNASSIGNED` and wait for the server. Do **not** fall back to the compiled
     slot.
   - **Missing record and authority never latched (pre-cutover only)** — fall
     back to the compiled `cube_id` as the slot (transitional).

Latching authority in NVS — not keying off the live marker each boot — is what
prevents split-brain: a cut-over cube that boots into a broker with lost retained
state, or before the server republishes the marker, still refuses fallback.

**The maintenance (timer-wake) path is not left untouched.** Today every
deep-sleep reset runs `getCubeIpOctet()` (which resets `cube_identifier` to the
compiled `cube_id`) and `handleWakeUp()` builds the keepalive client ID,
`cube/{id}/status`, and `cube/{id}/auto_sleep` from that compiled value before
full MQTT setup — so a reassigned spare would revert to its old logical identity
on every maintenance wake. The fix:

- Keepalive/status/auto-sleep identity becomes **MAC-scoped** (client ID and
  `cube/device/{MAC}/...` topics), never slot-scoped.
- Before any slot-scoped publish, the maintenance client reads retained
  `cube/assign/{MAC}` and validates it against the NVS `{slot, generation}`; on
  mismatch or a newer generation it defers to a full-client start rather than
  acting on the old slot.
- Before graceful deep sleep the cube publishes retained presence
  `state: sleeping`, so the gate never mistakes a sleeping cube for a live one.
  Sleep timing (10-min inactivity, timer wake) is unchanged.

## NFC neighbor identity

A cube reads the tag of its **right neighbor** and publishes it. Today it also
resolves that tag to a cube number on-device via the compiled
`lookupCubeNumberByTag` and publishes `cube/right/{id}`.

In the pool model the tag→slot mapping is dynamic, so resolution moves to the
server. The raw observation must also be MAC-scoped and provenance-tagged — the
old slot-scoped retained `cube/nfc/{id}` would let a retained or in-flight
observation from the prior holder be mapped through the new `slot → MAC` entry.

- The cube publishes its raw neighbor tag under a **MAC-scoped, non-retained**
  topic `cube/device/{MAC}/nfc` with provenance: sender MAC, `boot_id`,
  `generation`, and a monotonic per-boot `sequence`. On-device tag→number
  resolution and `cube/right/{id}` are retired for pooled identity.
- The server accepts an observation only when its sender MAC and `generation`
  match the current assignee for that slot, then resolves raw tag → slot using a
  `MAC → tag` value recorded once at commissioning (a spare brings its own tag,
  so it is entered on the console before assignment — one field, not a reflash)
  and the live `slot → MAC` map.
- The game-start gate stays closed until every assigned cube has supplied a
  **fresh post-assignment** snapshot for its current generation, so no stale
  neighbor state survives a replacement. Retained observations from a prior
  generation are cleared on reassignment.

## What this drops from the previous design

Epochs, leases, manifests, SHA-256 digests, quarantine states, power intents,
the Pi-boot-scoped time authority, absolute wake deadlines, the exactly-once
reboot NVS state machine, the atomic startup topology barrier, and the
DHCP/mDNS migration.

What it keeps small but does add, for correctness against the current firmware
and MQTT's retained/aliasing semantics: a unique per-MAC `ip_octet`; a
MAC-derived MQTT client ID; a per-device `generation`; MAC-scoped presence plus
a one-shot liveness challenge at the gate; an NVS-latched authority marker; and
MAC-scoped, provenance-tagged NFC snapshots. **Sleep timing is unchanged, but
the sleep/keepalive path is not left untouched** — its client identity and
topics move from the compiled slot to the MAC, and it publishes `sleeping`
before deep sleep. These are the irreducible core of proving *which* physical
cube holds a slot and that it is live; none of the epoch/lease/manifest/
time-authority machinery returns.

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
| Failed cube returns after reassignment | Reads `unassigned`, sits idle; own `ip_octet` and MAC client ID mean no IP or broker collision |
| Old holder + replacement both online | Distinct client IDs and IPs; only the assigned MAC answers the gate |
| Spare assigned to a taken slot | Server clears the slot from the prior MAC, bumping only the two devices' generations |
| Crash between the two swap publishes | Restart reloads the persisted map and republishes both devices at their stored generation |
| One-slot swap, other cubes unchanged | Unchanged cubes keep their generation and still pass the gate |
| Wrong-size spare assigned | Console rejects: hardware class must match |
| Stale/old holder tries to satisfy the gate | Gate needs a live challenge response at the assigned generation; retained `online` is not liveness |
| Assigned cube asleep at game-start | Publishes `sleeping`; gate stays closed until it is awake and answers the challenge |
| Not all assigned MACs prove liveness | Round does not start; console shows which are missing |
| Server restart | Reload the map from disk; re-issue liveness challenges (no cube reboot needed) |
| Missing assignment after authority latched | Firmware fails closed to `UNASSIGNED`; no compiled-slot fallback, even if the marker is absent |
| Reassigned spare wakes from deep sleep | Maintenance path is MAC-scoped and revalidates assignment; never reverts to old slot |
| Stale retained NFC from prior holder | Rejected by sender MAC/generation; gate waits for a fresh post-assignment snapshot |

## Implementation areas

### `cube-pn5180`

- Add a unique `ip_octet` field to `CubeMacEntry`, independent of `cube_id`;
  derive the static IP from it. Validate one octet per commissioned MAC.
- Use a MAC-derived MQTT client ID for both clients, from the first connection.
- Read logical slot from retained `cube/assign/{MAC}` at boot instead of the
  compiled `cube_id`; persist the applied `{slot, generation}` in NVS.
- Latch `cube/roster/authoritative` in NVS on first observation; after that,
  a missing/malformed assignment fails closed to `UNASSIGNED` (never the compiled
  slot), regardless of marker absence.
- Publish retained MAC-scoped presence (`cube/device/{MAC}/presence`) with an
  offline last will, and answer non-retained liveness challenges while awake.
- Move the timer-wake keepalive/status/auto-sleep identity to MAC-scoped client
  ID and topics; revalidate the retained assignment against NVS before any
  slot-scoped publish; publish `state: sleeping` before deep sleep.
- Publish MAC-scoped, provenance-tagged NFC (`cube/device/{MAC}/nfc` with sender
  MAC, boot ID, generation, sequence); retire on-device tag→number resolution and
  `cube/right/{id}`.

### `cubes`

- Add a persisted `MAC → {slot, generation}` map and a `MAC → tag` inventory; the
  on-disk map is the source of truth. Bump only the affected devices' generations
  on a swap.
- Perform swaps as a durable transaction: persist first, then publish the two
  affected assignment records; on restart, reload and idempotently republish
  every device at its stored generation.
- Publish the retained `cube/roster/authoritative` marker at cutover.
- Game-start gate: per required slot, require a fresh liveness-challenge response
  from the assigned MAC at its assigned generation, plus a fresh post-assignment
  NFC snapshot; do not accept retained `online` as liveness.
- Resolve NFC neighbor tags server-side from `MAC→tag` × `slot→MAC`, rejecting
  observations whose sender MAC/generation is not the current assignee; stop
  consuming `cube/right/{id}`.
- Never change a slot assignment from heartbeat expiry or sleep.

### `pi-deploy`

- Persist the `MAC → {slot, generation}` map and `MAC → tag` inventory across
  restarts.
- Add a console `assign-slot` action (and a `swap` shortcut: assign a failed
  cube's slot to a spare and clear the old holder), with hardware-class checks.
- Assign and record a unique `ip_octet` per commissioned MAC.

## Test plan

### Server unit tests

- Assigning a taken slot clears it from the prior MAC (one MAC per slot) and
  bumps only the two affected devices' generations.
- A one-slot swap leaves every unchanged cube able to satisfy the gate (its
  generation did not move).
- Heartbeat expiry and sleep never change an assignment.
- A returning cube whose slot was reassigned resolves to `unassigned`.
- Wrong-hardware-class assignment is rejected.
- The gate opens only when every required slot's *assigned MAC* returns a fresh
  liveness response at its assigned generation plus a fresh NFC snapshot; retained
  `online`, a stale/old holder, or a sleeping cube never satisfies it.
- A crash after persisting the map but before (or between) the publishes recovers
  on restart: the map reloads and both devices republish at their stored
  generation (no double-claim, no missing half).
- Server restart re-issues liveness challenges and completes the gate without a
  cube reboot.
- An NFC observation whose sender MAC/generation is not the current assignee is
  rejected.

### Firmware-native tests

- A cube uses the assigned slot for topics and rotation, not the compiled
  `cube_id`, and persists `{slot, generation}` to NVS.
- The MQTT client ID is MAC-derived from the first connection; two cubes sharing
  a default slot do not disconnect each other at the broker.
- Once `cube/roster/authoritative` is latched in NVS, a missing/malformed
  assignment fails closed to `UNASSIGNED` even when the marker is later absent.
- Without a latched marker (pre-cutover), a missing record falls back to the
  compiled `cube_id`.
- An `unassigned` record produces the idle state: no slot subscriptions, no
  neighbor/sensor publication.
- The static IP comes from the per-MAC `ip_octet`, unique per physical cube,
  regardless of slot.
- After reassignment, a timer wake uses the MAC-scoped client ID/topics, does not
  publish under the old slot, and revalidates the assignment first.
- The cube publishes `state: sleeping` before deep sleep, answers a liveness
  challenge while awake, and publishes provenance-tagged NFC on
  `cube/device/{MAC}/nfc`; it no longer publishes `cube/right/{id}`.

### Broker / hardware acceptance

- Replace one cube in a 6-cube game via the console and a reboot; verify the
  spare plays the failed slot and forms words on both left and right edges, and
  that the other five slots still open the gate.
- Power the failed primary and its active backup on **simultaneously**; verify
  distinct IPs and MQTT client IDs (no `.21` conflict, no broker eviction) and
  that only the assigned MAC answers the gate.
- Kill the server between the two swap publishes, restart it, and verify the swap
  converges without a double-claimed slot.
- Restart only the server (cubes untouched) and verify the gate re-completes via
  re-issued liveness challenges.
- Reassign a spare, let it deep sleep, and verify a maintenance wake never
  re-publishes under its old slot and that the gate treats it as `sleeping`.
- Repeat for a 12-cube game across both player sets.
- Verify small↔small and large↔large replacement; confirm cross-size is rejected.
- Leave cubes idle past the sleep threshold; verify no assignment change.

## Rollout

1. Assign a unique `ip_octet` per commissioned MAC in `CubeMacEntry`, and switch
   both MQTT clients to MAC-derived client IDs; ship it so every physical cube has
   a distinct address and client ID (no shared-`cube_id` collisions).
2. Add the server `MAC → {slot, generation}` map, `MAC → tag` inventory, and
   retained `cube/assign` publishes, seeded to match today — no behavior change.
   The authority marker stays off, so firmware fallback still applies.
3. Ship firmware that reads its slot from the assignment (compiled `cube_id`
   fallback until authority latches), persists `{slot, generation}` to NVS,
   publishes MAC-scoped presence, answers liveness challenges, and uses MAC-scoped
   keepalive on wake.
4. Add the console assign/swap action and the MAC-verified, liveness-gated
   game-start gate, then publish `cube/roster/authoritative` — after which every
   cube latches it and fails closed instead of falling back.
5. Move neighbor tag resolution to the server with provenance checks; retire
   compiled `lookupCubeNumberByTag` and `cube/right/{id}` for pooled identity.

Steps 1–3 preserve behavior, so there is no flag day. Mixed firmware is safe
until step 4 (authority cutover), at which point every field cube must be on the
assignment-aware build.

## Open decisions before implementation

1. Where do the `MAC → {slot, generation}` map and `MAC → tag` inventory live on
   the Pi, and what is the durable-write mechanism (single-file atomic replace)?
2. How does the console capture a spare's tag and unique `ip_octet` at
   commissioning?
3. Confirm hardware-class metadata (small/large) per commissioned cube.
4. Liveness-challenge bounds: nonce interval and response timeout that reliably
   cover a just-woken cube's Wi-Fi/MQTT startup on battery.
4. Confirm which display state must be re-sent after a slot change (most
   `cube/{slot}/...` display topics are retained and arrive on subscribe).
5. Confirm the NVS namespace/keys for the applied `{slot, generation}`, the
   latched authority bit, and their
   interaction with existing sleep-state persistence.
