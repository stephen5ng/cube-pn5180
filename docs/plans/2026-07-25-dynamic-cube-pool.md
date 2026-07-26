# Dynamic Cube Pool and Automatic Replacement

**Status:** Proposed design; no implementation
**Date:** 2026-07-25
**Owners:** `cube-pn5180` firmware, `cubes` game server, and `pi-deploy`

## Decision

Treat every cube as a member of a hardware-compatible device pool. There is no
permanent `primary` or `standby` role and no explicit release operation.

For a six-cube game, the server selects six compatible online cubes. For a
twelve-cube game, it selects twelve. Extra compatible cubes remain idle. If an
active cube disappears, the server assigns an idle cube of the same hardware
class to the vacated logical slot. If the old cube later returns, it remains
idle while the replacement keeps its lease.

This is conceptually “take the first six cubes,” with two safety refinements:

1. selection happens after a short discovery window rather than literal MQTT
   arrival order;
2. assignments are sticky, so a late or returning cube never reshuffles a
   healthy running roster.

## Constraints

- A game uses 6 cubes for single-player or 12 cubes for two-player.
- Small and large cubes are not physically interchangeable.
- A device may replace only a slot requiring the same hardware compatibility
  class.
- Full replacement coverage requires at least one extra small cube and one
  extra large cube when both sizes are deployed.
- The current firmware derives logical identity from both ESP32 MAC address
  and enclosure NFC tag. Both must become runtime assignments.

## Goals

- All cubes run the same role-free firmware for their hardware build.
- Powering on a compatible extra cube is sufficient to make it available.
- The server automatically maintains a roster of 6 or 12 active cubes.
- A replacement adopts the failed cube's exact logical slot, MQTT topics,
  player grouping, display rotation, and current retained display state.
- A returning cube cannot evict a healthy replacement during a game.
- Normal restarts preserve stable assignments when the same cubes return.
- The system fails closed when there are too few compatible cubes or an
  ambiguous/conflicting assignment.
- No `cube/standby/release` topic or field maintenance release is required.

## Non-goals

- Using a small cube to replace a large cube or vice versa.
- Continuing normally when the compatible pool has fewer than 6 or 12 devices
  required by the selected game mode.
- Replacing multiple failed cubes when the pool does not contain enough
  compatible extras.
- Preserving formula-based IP addresses for dynamically assigned devices.
- Replacing the existing MQTT broker security model.

## Identity model

Separate stable physical identity from session-facing logical identity:

| Identifier | Example | Lifetime |
|---|---|---|
| Device ID | `80F3DA5453B8` | ESP32 lifetime; derived from MAC |
| NFC tag ID | 16 hex characters | Enclosure lifetime |
| Hardware class | `small` or `large` | Immutable commissioning metadata |
| Logical slot | `1-6` or `11-16` | Server-managed sticky lease |
| Session epoch | Server-generated ID | One roster generation |

No device is intrinsically cube 4 or intrinsically a standby. “Cube 4” means
the online device currently holding logical slot 4.

Hardware class should be extensible beyond size if later hardware differences
also prevent substitution. For example, a future value might encode size,
display electrical profile, and enclosure generation rather than only
`small`/`large`.

## Session configuration

The assignment manager must know the desired roster before selecting devices:

```text
Single-player slots: 1, 2, 3, 4, 5, 6
Two-player slots:    1, 2, 3, 4, 5, 6, 11, 12, 13, 14, 15, 16
```

The game configuration must provide:

- `cube_count`: `6` or `12`;
- the hardware class required for each slot, or a single class when all slots
  use the same size.

Explicit configuration is preferable to counting online devices. With seven
cubes online, presence alone cannot determine whether the operator intends a
six-cube game plus one extra or is still booting a twelve-cube game.

For migration, current logical IDs seed each device's last-known slot. That
preserves the existing player grouping in normal operation while removing the
permanent primary/standby distinction. New or replacement devices have no
initial slot preference.

## Discovery and roster selection

### Discovery

Every powered cube:

1. connects using DHCP;
2. uses an MQTT client ID derived from its stable device ID;
3. publishes retained device metadata, installs a retained offline last will,
   and starts a non-retained heartbeat containing its per-boot nonce;
4. waits for a server assignment before using logical cube topics;
5. displays `AVAILABLE` while online but not selected.

The server waits for a five-second discovery window on cold startup. It then
builds a roster from devices whose hardware classes satisfy the configured
slot requirements.

Retained `online: true` presence is descriptive state, not proof that a device
is currently reachable. A device becomes eligible only after the server
receives a non-retained heartbeat after subscribing, with a `boot_id` matching
the device's current presence record. Heartbeats include a monotonically
increasing `sequence`; a duplicate or older sequence for the same boot is
ignored. This prevents stale retained presence from entering a roster during
the five-second discovery window after a server restart.

### Deterministic selection

The server does not use raw MQTT arrival order. It ranks eligible devices:

1. online devices already holding a valid lease for this roster;
2. online devices requesting a unique last-known slot;
3. remaining compatible devices ordered by stable device ID.

It assigns exactly the configured 6 or 12 slots. Compatible devices beyond
that count stay `AVAILABLE`.

Sticky leases matter more than the initial ranking: after a roster is active,
a newly appearing device cannot evict an active lease holder.

### Player grouping

For a twelve-cube game, slots `1-6` remain one physical cube set and slots
`11-16` remain the other. Persisted last-known slots keep today's grouping
stable across normal restarts.

If the server must build a completely new twelve-cube roster without any slot
history, it needs an explicit grouping method. The first version should use
commissioned `preferred_set` metadata (`0`, `1`, or `either`) and require six
compatible devices per set. Arbitrarily splitting twelve devices by boot order
could scatter player-0 and player-1 assignments across two physical play
areas.

An extra intended to replace either player set uses `preferred_set: either`.
This is a placement preference, not a primary/standby role.

## Automatic replacement

An active device is considered unavailable after:

- an MQTT offline last will, or
- six seconds without a valid live heartbeat.

The server then waits a short stability interval so a transient reconnect does
not churn the roster. If the device remains unavailable:

1. revoke the failed device's old lease commit, which immediately quarantines
   that lease and disables its NFC alias;
2. keep its logical slot open;
3. select the highest-ranked idle device matching the slot's hardware class
   and permitted player set;
4. publish prepared slot owner, device assignment, and lease-bound NFC alias
   records for a new lease;
5. publish the new lease's activation commit only after all prepared records
   have reached the broker;
6. wait for a live acknowledgement matching the device, boot, slot, roster
   epoch, and lease;
7. replay any non-retained state the replacement needs.

Owner, assignment, and alias records alone never authorize firmware activity.
The lease commit is the final activation record. If the server crashes at any
point before it publishes that commit, the replacement remains quarantined.
On reconnect, every consumer requires all records to agree before using the
lease, so retained-message delivery order cannot create a partial activation.

The replacement receives the exact vacated slot rather than causing the other
five or eleven cubes to be renumbered.

If no compatible idle device exists, the slot remains unavailable and the
server reports the required hardware class. It must not assign an incompatible
cube.

## Returning and surplus cubes

A device that comes online after its old slot has been reassigned publishes
presence normally but receives no active assignment. It displays `AVAILABLE`
and does not subscribe or publish through logical cube topics.

Revoking the old lease also invalidates its tag alias. The server publishes an
inactive retained tombstone for the former holder's tag before preparing the
replacement. Pooled firmware never falls back to the compiled tag table for a
commissioned pooled tag: an active, lease-bound runtime alias is required.
This prevents an unassigned cube placed near another cube from appearing as
its former logical slot.

There is no automatic “original wins” behavior. Reclaiming the slot would
interrupt the replacement and could corrupt neighbor state during a live game.

There is also no explicit release:

- if the replacement stays online, it keeps the slot;
- if the replacement later powers off and the former device is available, the
  former device can automatically fill the open slot;
- on a cold roster rebuild, sticky history and deterministic ranking select
  the active 6 or 12 devices;
- an optional administrative “rebuild roster” command may exist for
  diagnostics, but normal field operation does not require it.

## MQTT protocol

All JSON examples are illustrative; final field names should be versioned.
Device IDs are uppercase MAC addresses without colons.

### Presence metadata

Topic:

```text
cube/device/{device_id}/presence
```

Retained online payload:

```json
{
  "protocol": 1,
  "online": true,
  "tag": "BD291466080104E0",
  "hardware_class": "small",
  "preferred_set": "either",
  "last_slot": 4,
  "firmware": "git-version",
  "boot_id": "random-per-boot"
}
```

Firmware updates this retained record on connection and installs a retained
`online: false` last will. The server never uses a retained replay of this
record as liveness proof.

### Live heartbeat

Topic:

```text
cube/device/{device_id}/heartbeat
```

Non-retained payload:

```json
{
  "protocol": 1,
  "boot_id": "random-per-boot",
  "sequence": 42
}
```

Firmware publishes immediately after connecting and every two seconds. Only a
heartbeat received live after the server subscribed, with a `boot_id` matching
current presence and a newer sequence, establishes or refreshes eligibility.

### Current roster epoch

Topic:

```text
cube/roster/current
```

Retained payload:

```json
{
  "protocol": 1,
  "epoch": "server-roster-epoch",
  "cube_count": 6
}
```

Firmware accepts only leases belonging to this epoch. A profile transition
prepares a complete new epoch and publishes this record last as the global
commit.

### Roster manifest

Topic:

```text
cube/roster/{epoch}/manifest
```

The retained manifest declares the configured cube count, selected devices,
slots, hardware classes, and lease IDs for that epoch. It lets the server
validate that a prepared roster is complete before committing it and
reconstruct the roster after restart.

### Slot ownership

Topic:

```text
cube/roster/{epoch}/slot/{logical_slot}/owner
```

Retained prepared payload:

```json
{
  "protocol": 1,
  "device_id": "80F3DA5453B8",
  "epoch": "server-roster-epoch",
  "lease_id": "server-generated-lease",
  "state": "prepared"
}
```

### Device assignment

Topic:

```text
cube/roster/{epoch}/device/{device_id}/assignment
```

Retained prepared payload:

```json
{
  "protocol": 1,
  "logical_slot": 4,
  "epoch": "server-roster-epoch",
  "lease_id": "same-as-owner",
  "state": "prepared"
}
```

An inactive retained tombstone means the device is unassigned. Firmware
persists the last slot for startup preference, but owner and assignment
agreement alone does not activate it.

### NFC alias

Topic:

```text
cube/roster/{epoch}/tag-alias/{tag_id}
```

Retained prepared payload:

```json
{
  "protocol": 1,
  "logical_slot": 4,
  "epoch": "server-roster-epoch",
  "lease_id": "same-as-owner",
  "state": "prepared"
}
```

Every selected device's tag receives a lease-bound alias. Firmware resolves
the alias only when its epoch is current and its lease has an active commit.
Revocation replaces it with a retained tombstone:

```json
{
  "protocol": 1,
  "state": "inactive"
}
```

Pooled firmware treats the runtime alias registry as authoritative for every
commissioned pooled tag. The compiled `KNOWN_TAGS` table may be used only
during the observe-only migration phase; after pooling is enabled, there is no
static fallback for a missing or inactive alias.

### Lease activation commit

Topic:

```text
cube/roster/{epoch}/lease/{lease_id}/commit
```

Retained active payload:

```json
{
  "protocol": 1,
  "state": "active",
  "device_id": "80F3DA5453B8",
  "logical_slot": 4,
  "epoch": "server-roster-epoch"
}
```

The server publishes this only after the matching owner, assignment, and alias
records are prepared. Revocation replaces it with `state: "revoked"`. Devices
and tag lookups require current epoch plus exact agreement across all four
records. The commit is therefore the final per-slot activation signal.

All retained roster records are scoped beneath their epoch. Preparing a new
6- or 12-cube profile therefore cannot overwrite owner, assignment, alias, or
lease records used by the current epoch.

### Assignment acknowledgement

Topic:

```text
cube/device/{device_id}/assignment-ack
```

Non-retained payload:

```json
{
  "protocol": 1,
  "state": "active",
  "boot_id": "random-per-boot",
  "logical_slot": 4,
  "epoch": "server-roster-epoch",
  "lease_id": "server-generated-lease",
  "sequence": 7
}
```

Firmware publishes an acknowledgement immediately after entering `ACTIVE`,
after every MQTT reconnect, and every two seconds while active. The server
accepts only a live acknowledgement matching the current presence `boot_id`
and exact current lease. Hydration begins only after this proof. On revocation,
firmware publishes the same shape with `state: "available"` and null slot and
lease fields.

## Roster profile transitions

Changing between 6 and 12 cubes creates a new roster epoch. The server never
edits the current epoch in place:

1. choose the complete target roster and new epoch;
2. publish the new epoch manifest;
3. prepare owner, assignment, alias, and active lease-commit records under the
   new epoch for every selected device, including carried-over devices;
4. verify the manifest and all prepared records have reached the broker;
5. publish `cube/roster/current` with the new epoch as the global commit;
6. wait for live acknowledgements from every selected device;
7. tombstone assignments and aliases and revoke leases left behind in the old
   epoch.

Before step 5, firmware continues using the old roster. At step 5, any device
without a complete matching lease in the new epoch immediately quarantines.
Thus a `12 -> 6` transition deactivates slots `11-16` even if their old
retained records remain during cleanup, while a `6 -> 12` transition activates
only the fully prepared twelve-device roster. A crash before step 5 leaves the
old roster authoritative; a crash after it leaves a complete new roster that
can be reconstructed from retained state.

Roster control records use retained QoS 1 publishes from one authoritative
server connection. The server waits for each publish acknowledgement before
publishing the per-lease or global commit. Consumers still validate the full
record set; ordering alone is never treated as authority.

## Firmware behavior

```text
BOOT
  -> DHCP + unique MQTT client ID
  -> publish presence and live heartbeat
  -> wait for current epoch and a complete committed lease

AVAILABLE
  -> display "AVAILABLE"
  -> no logical cube subscriptions or sensor publishes

ACTIVE
  -> require current epoch + matching owner, assignment, alias, and commit
  -> apply slot-specific player rotation
  -> subscribe to cube/{slot}/...
  -> publish NFC/right/sensor state as {slot}
  -> publish live assignment acknowledgement

QUARANTINED
  -> stop logical publishes immediately on epoch/owner/assignment/alias/commit mismatch
  -> discard cached aliases and lease records from a superseded epoch
  -> clear local logical neighbor state
  -> return to AVAILABLE
```

Logical-topic and tag-alias gating are mandatory. Without them, a returning
device could publish `cube/right/{old_slot} = "-"` or be recognized through
its old static tag mapping and corrupt the replacement's neighbor state.

All pooled devices use DHCP. Static IP `192.168.8.{20+slot}` cannot safely
follow a runtime lease because a returning device may temporarily believe the
same last-known slot. Maintenance and OTA tooling should use mDNS plus device
ID.

## State hydration

Most display commands already use retained `cube/{slot}/...` topics. The
replacement should receive those immediately after activating its slot.

Implementation must inventory every required state field:

- letter;
- borders and highlight;
- lock;
- brightness;
- sleep state and interval;
- any game-specific transient display mode.

Anything not retained must be replayed by the game server after a live
assignment acknowledgement for the exact committed lease. The replacement
must publish fresh NFC/right state rather than inheriting retained sensor state
from the failed device.

## Failure behavior

| Situation | Result |
|---|---|
| 6 required, 7 compatible online | 6 active, 1 available |
| 12 required, 13 compatible online | 12 active, 1 available |
| Active cube fails and compatible extra exists | Extra takes exact slot |
| Failed cube returns after replacement | Returning cube stays available |
| Wrong-size extra is online | It remains available; no assignment |
| Too few compatible cubes | Missing slot remains unfilled |
| Broker/server unavailable | Existing complete leases continue; no new assignments |
| Duplicate/conflicting lease | Both claimants quarantine until resolved |
| Transient heartbeat loss | Stability interval prevents immediate churn |
| Retained online presence without a live heartbeat | Device is ineligible |
| Server crashes before a lease commit | Prepared device stays quarantined |
| Server crashes before a new epoch commit | Old roster remains authoritative |
| 12-cube profile changes to 6 | Slots `11-16` quarantine at epoch commit |

## Inventory and commissioning

The authoritative inventory records physical facts and preferences, not
primary/standby roles:

```text
device_id
nfc_tag
hardware_class
board_version
rgb_order
hall_sensor_profile
preferred_set: 0 | 1 | either
last_commissioned_firmware
```

The inventory must validate:

- unique device IDs and NFC tags;
- known hardware classes;
- compatible firmware build and board metadata;
- enough eligible devices for each supported 6- or 12-cube configuration;
- at least one extra of each size when automatic replacement coverage is
  promised.

Current candidate `80:F3:DA:54:53:B8` still needs its size, board profile, and
attached NFC tag verified. `CC:DB:A7:99:0F:E0` is recorded as unable to enter
download mode and should not count as available capacity.

## Implementation areas

### `cube-pn5180`

- Replace MAC-to-logical-ID boot identity with stable device identity.
- Add current-epoch, assignment, owner, alias, and lease-commit validation plus
  NVS last-slot preference.
- Add unique MQTT client IDs, non-retained live heartbeat, retained presence,
  and last will.
- Publish lease-specific activation acknowledgements.
- Gate every logical subscription and publish behind the active lease.
- Add lease-bound runtime NFC aliases for all active cubes, inactive
  tombstones, and disable compiled tag fallback in pooled mode.
- Move pooled networking to DHCP and add mDNS device naming.
- Add `AVAILABLE`, active slot, and quarantine diagnostics.

### `cubes`

- Add a persistent roster/lease manager with an injectable clock.
- Make desired cube count and slot hardware classes explicit game config.
- Load and validate device inventory and preferred player sets.
- Publish prepared owner, assignment, and tag-alias state followed by explicit
  lease or roster-epoch commits.
- Validate an epoch manifest before making it current.
- Require live boot-matched heartbeats for eligibility and exact live
  acknowledgements before hydration.
- Implement epoch transitions and cleanup for `6 -> 12` and `12 -> 6`.
- Rehydrate non-retained cube state after replacement.
- Expose active, available, missing, and incompatible devices in logs/admin UI.

### `pi-deploy`

- Install the authoritative inventory and persist roster state.
- Configure the desired 6- or 12-cube session profile.
- Update monitoring and OTA tools to locate devices by mDNS/device ID.
- Preserve MQTT retained ownership and assignment topics across service
  restarts, including roster epochs, lease commits, and alias tombstones.

## Test plan

### Server unit tests

- Six-cube mode selects exactly six from seven compatible devices.
- Twelve-cube mode selects exactly twelve from thirteen.
- MQTT arrival order does not affect deterministic cold selection.
- An active roster does not change when another device comes online.
- A failed slot receives a matching-class idle device after timeout.
- A wrong-size device never receives the slot.
- A returning former holder does not evict its replacement.
- Replacement power-off allows another compatible available device, including
  the former holder, to fill the slot.
- Sticky last-slot preferences preserve player grouping.
- Duplicate slot preferences resolve deterministically and safely.
- Too few devices leave explicit missing slots.
- Retained `online: true` presence without a post-subscription, boot-matched
  heartbeat never establishes freshness.
- Stale, malformed, duplicated, and out-of-order heartbeat sequences are
  ignored safely.
- Prepared owner/assignment/alias records cannot activate without the exact
  lease commit.
- An incomplete or internally inconsistent epoch manifest cannot become
  current.
- Acknowledgements with the wrong boot, slot, epoch, lease, or sequence do not
  trigger hydration.
- Revocation tombstones the former tag alias before replacement preparation.
- `6 -> 12` and `12 -> 6` build complete new epochs and quarantine devices
  removed from the new profile.
- A crash before an epoch commit preserves the old roster; a crash after the
  commit reconstructs the complete new roster.
- Server restart reconstructs persisted or retained leases.

### Firmware-native tests

- Assignment, owner, alias, commit, and current roster must match device, slot,
  epoch, and lease.
- Lease loss immediately disables logical topics.
- Last-slot NVS state is a preference, never authority.
- Pooled commissioned tags never fall back to legacy static mappings.
- Inactive alias tombstones and revoked commits both prevent tag resolution.
- A returning unassigned cube's physical tag cannot resolve to its last slot.
- Firmware remains quarantined after prepared records and activates only after
  the final commit.
- Assignment acknowledgement contains the exact boot, slot, epoch, and lease.
- Hardware class cannot be changed by an assignment.
- Slot `1-6` and `11-16` select correct display rotation.

### Broker integration tests

- Start 7 devices in six-cube mode and verify one remains available.
- Kill one active device and verify the available device adopts its exact
  logical MQTT topics.
- Reconnect the old device and verify it cannot publish slot state.
- Repeat with 13 devices in twelve-cube mode.
- Repeat replacement with matching and mismatched size pools.
- Restart the broker, game server, and individual devices independently.
- Restart the server with stale retained `online: true` presence and verify no
  device is eligible until a fresh matching heartbeat arrives.
- Interrupt replacement after every preparation step and verify the candidate
  remains quarantined until the commit exists.
- Transition `6 -> 12` and `12 -> 6`; verify removed devices stop logical
  traffic and aliases at the epoch flip.
- Interrupt profile transition before and after the epoch commit and verify
  recovery selects the old or new complete roster, never a partial mix.
- Reconnect firmware with `cube/roster/current` delivered before the new
  epoch's records and verify it stays quarantined until the complete lease
  arrives.
- Verify retained display hydration and fresh neighbor-state publication.

### Hardware acceptance

- Run six-cube single-player with a compatible extra powered on.
- Run twelve-cube two-player with compatible extras.
- Replace representative slots in both player sets.
- Test small-to-small and large-to-large replacement.
- Confirm small-to-large and large-to-small assignment are impossible.
- Form words with the replacement on both left and right edges.
- Power the old cube back on elsewhere on the LAN during a game.
- Verify player grouping and rotation after full cold startup.
- Acceptance target: replacement ready within 15 seconds of confirmed failure.

## Rollout

1. Create and validate authoritative device inventory.
2. Deploy unique device identity and presence in observe-only mode.
3. Add roster selection while retaining current static logical behavior.
4. Deploy runtime ownership gating and NFC aliases to every field cube.
5. Seed last-slot preferences from current IDs.
6. Switch pooled cubes to DHCP and update maintenance tooling.
7. Enable dynamic selection for six-cube sessions.
8. Verify sticky player grouping, then enable twelve-cube sessions.
9. Commission at least one compatible extra per deployed size.

Mixed old/new firmware is unsafe once dynamic assignment starts. An old cube
does not understand lease gating and could publish through a slot owned by
another device.

## Open decisions before implementation

1. Where should the authoritative device inventory and persisted roster live?
2. How is `cube_count` selected for each event or game launch?
3. Are slot hardware classes uniform per session, uniform per player set, or
   mixed? Record the actual small/large layout.
4. Confirm the `preferred_set` values needed to preserve physical two-player
   grouping.
5. Confirm DHCP capacity and mDNS support on the field router.
6. Inventory which logical state topics are not currently retained.
7. Confirm the size, board profile, RGB order, Hall profile, and NFC tag of
   each extra cube candidate.
