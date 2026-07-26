# Dynamic Cube Pool with Boot-Time Replacement

**Status:** Proposed design; no implementation
**Date:** 2026-07-26
**Owners:** `cube-pn5180` firmware, `cubes` game server, and `pi-deploy`

## Decision

Treat every cube as a member of a hardware-compatible device pool, with no
permanent `primary` or `standby` role. Roster membership changes only during an
administrator-triggered rebuild that reboots all cubes and interrupts the
game.

For a six-cube game, the rebuild selects six compatible cubes. For a
twelve-cube game, it selects twelve. Extra compatible cubes remain available.
If an active cube fails during play, the server reports the failure but does
not change the roster. An administrator powers on a compatible extra, powers
off or excludes the failed cube, and starts a full roster rebuild.

This deliberately trades hot replacement for a much smaller and safer first
version:

- intentional deep sleep can never trigger replacement;
- gameplay is not running while neighbor topology changes;
- one immutable roster is committed at boot;
- returning and delayed devices cannot alter the roster;
- no explicit spare release operation is needed.

## Field workflow

1. Stop the game after a cube failure.
2. Power off or remove the failed cube. If it is intermittently online, record
   its device ID in the rebuild exclusion list.
3. Put a compatible extra cube in its place and power it on.
4. Run the administrative `rebuild-cube-roster` action for 6 or 12 cubes.
5. The action publishes a retained rebuild barrier and requests every cube to
   reboot.
6. The server waits for newly booted devices, selects a complete compatible
   roster, and commits one new roster epoch.
7. The server gathers a complete sensor snapshot, installs the neighbor graph
   atomically, and only then enables ABC/gameplay.

No flashing, per-cube ID selection, or release command is required in the
field. The replacement does require a full game interruption and cube reboot.

## Constraints

- A game uses 6 cubes for single-player or 12 cubes for two-player.
- Small and large cubes are not physically interchangeable.
- A device may fill only a slot requiring the same hardware compatibility
  class.
- Full coverage requires at least one extra small cube and one extra large
  cube when both sizes are deployed.
- The desired cube count and slot hardware classes must be explicit rebuild
  inputs; online-device count alone is ambiguous.
- Current firmware derives logical identity from both ESP32 MAC address and
  enclosure NFC tag. Both become roster-time assignments.
- Current firmware auto-sleeps after 10 minutes of MQTT inactivity and briefly
  wakes about every 20 seconds. The pooled lifecycle must preserve this
  battery behavior.

## Goals

- All cubes run the same role-free firmware for their hardware build.
- An administrator can replace a failed cube by powering a compatible extra
  and rebuilding the roster.
- A rebuild deterministically selects exactly 6 or 12 compatible devices.
- The committed roster is immutable until another administrative rebuild.
- Sleeping, offline, returning, and extra devices never cause automatic
  reassignment.
- A selected device adopts its assigned MQTT command topics, player grouping,
  display rotation, and retained display state.
- Logical neighbor state is derived from device-scoped, lease-provenanced
  physical observations.
- ABC, guessing, and scoring remain disabled until the complete new topology
  is installed atomically.
- Server and broker restarts reconstruct the same active roster unless a
  rebuild was already in progress.
- No `cube/standby/release` operation is required.

## Non-goals

- Replacing a cube without interrupting the current game.
- Changing roster membership because of a heartbeat timeout.
- Using a small cube to replace a large cube or vice versa.
- Continuing a rebuild with fewer than the configured 6 or 12 compatible
  devices.
- Preserving formula-based IP addresses for dynamically assigned devices.
- Replacing the existing MQTT broker security model.

## Identity model

Stable physical identity is separate from roster identity:

| Identifier | Example | Lifetime |
|---|---|---|
| Device ID | `80F3DA5453B8` | ESP32 lifetime; derived from MAC |
| NFC tag ID | 16 hex characters | Enclosure lifetime |
| Hardware class | `small` or `large` | Immutable commissioning metadata |
| Preferred set | `0`, `1`, or `either` | Commissioning preference |
| Logical slot | `1-6` or `11-16` | One committed roster epoch |
| Lease ID | Server-generated ID | One device assignment in one epoch |
| Rebuild ID | Server-generated ID | One administrative rebuild attempt |

No device is intrinsically cube 4 or intrinsically a spare. “Cube 4” means the
device assigned logical slot 4 in the active roster.

Hardware class should remain extensible if later electrical or enclosure
differences also prevent substitution.

## Session profiles

```text
Single-player slots: 1, 2, 3, 4, 5, 6
Two-player slots:    1, 2, 3, 4, 5, 6, 11, 12, 13, 14, 15, 16
```

The rebuild request supplies:

- `cube_count`: `6` or `12`;
- required hardware class for each slot, or one class for the whole profile;
- optional `excluded_device_ids`;
- optional timeout override.

For a twelve-cube game, slots `1-6` remain one physical player set and slots
`11-16` remain the other. Current IDs seed last-slot preferences during
migration. Commissioned `preferred_set` metadata prevents a completely new
roster from scattering the two logical sets across physical play areas. Extras
that may replace either side use `preferred_set: either`.

## Administrative roster rebuild

### Preconditions

- Gameplay, ABC detection, guessing, and scoring are paused.
- The compatible extra is powered.
- The failed or unwanted device is powered off or explicitly excluded.
- The requested profile identifies 6 or 12 slots and their hardware classes.

### Rebuild barrier

The server first publishes a retained `REBUILDING` control record containing a
new rebuild ID, desired profile, previous epoch, exclusions, and start time.

Every device subscribes to roster control regardless of whether it is active
or available. An awake device immediately enters `ENROLLING` and honors a
non-retained broadcast reboot request. A sleeping cube sees the retained
barrier on its next timer wake and performs a full boot instead of returning
to sleep.

The enrollment window must exceed the existing timer-wake interval plus
network startup margin. The default is 30 seconds. A physical all-cube power
cycle may be used instead of waiting for timer wakes.

### Fresh enrollment

After reboot, each device:

1. connects using DHCP and a device-derived MQTT client ID;
2. reads the retained rebuild control;
3. generates a new `boot_id`;
4. publishes retained device metadata;
5. publishes a non-retained heartbeat containing the rebuild and boot IDs;
6. remains `ENROLLING` without subscribing to logical commands or publishing
   game-authoritative sensor state.

A device is eligible only after the server receives a live heartbeat for the
current rebuild ID and matching `boot_id`. Retained `online: true` presence is
never enrollment proof.

### Deterministic selection

After the enrollment window, the server ranks compatible, non-excluded
devices:

1. devices from the previous roster requesting a unique compatible last slot;
2. other devices requesting a unique compatible last slot;
3. remaining compatible devices ordered by stable device ID.

It selects exactly the required 6 or 12 devices. Extras stay `AVAILABLE`.
Selection never changes after the roster becomes active.

If capacity or player-set constraints cannot produce a complete roster, the
server leaves control in `REBUILDING`, keeps gameplay paused, and reports the
missing hardware classes or player-set capacity. It never commits a partial
roster.

### Prepare and commit

The server creates a complete immutable epoch:

1. publish the epoch manifest;
2. publish owner, assignment, tag-alias, and lease records for every selected
   device;
3. verify the manifest and all records reached the broker at QoS 1;
4. publish roster control with `state: "ACTIVE"` and the new epoch as the
   single global commit.

Before step 4, all devices remain `ENROLLING`. At step 4, a selected device
activates only if its manifest, owner, assignment, own tag alias, lease, rebuild
ID, and control epoch all agree. Unselected devices become `AVAILABLE`.

The previous epoch remains immutable and non-authoritative. It may be
garbage-collected after the new roster and topology are confirmed.

### Startup topology barrier

Committing the roster does not immediately enable gameplay. The server opens a
startup topology barrier:

1. selected devices acknowledge the exact boot, rebuild, epoch, slot, and
   lease;
2. every selected device publishes a fresh raw sensor snapshot under that
   provenance;
3. the server validates all snapshots and builds a staged neighbor graph;
4. the server replaces the coordination graph in one bulk operation;
5. only after the bulk install does it enable ABC detection and gameplay.

Per-device snapshots never call normal neighbor-change handlers while the
barrier is active. No intermediate `-` edges can trigger word evaluation,
guesses, scoring, or ABC transitions.

If required acknowledgements or snapshots time out, gameplay stays paused and
the admin UI identifies the missing devices. The server never exposes a
partially rebuilt graph.

## Sleep lifecycle

Roster membership is independent of liveness after commit. Heartbeat expiry
updates health status but never changes owner, assignment, alias, lease, or
manifest records.

Before an active cube enters deep sleep, it publishes device-scoped sleep
state:

```text
cube/device/{device_id}/sleep-state
```

```json
{
  "protocol": 1,
  "state": "sleeping",
  "epoch": "active-epoch",
  "lease_id": "active-lease",
  "next_wake_ms": 20000
}
```

The active lease remains valid while the device sleeps. The server reports the
device as `SLEEPING`, not failed or available.

On timer wake, the minimal maintenance client:

- uses the stable device ID rather than the logical slot as its MQTT client and
  topic identity;
- reads retained roster control;
- if control is still `ACTIVE` for its epoch, publishes updated device-scoped
  sleep status and may return to sleep;
- if control is `REBUILDING` with a new rebuild ID, stays awake, performs the
  full enrollment boot, and does not return to sleep;
- never publishes the legacy logical `/status` keep-alive.

Device-scoped wake and sleep commands replace logical auto-sleep flags for the
maintenance path. Logical game commands remain lease-gated while the cube is
fully awake.

An actually failed cube and an intentionally sleeping cube may both lack
heartbeats, but neither causes reassignment. Replacement requires the explicit
administrative rebuild.

## Returning and surplus cubes

A device that appears after the enrollment window cannot join the active
roster. It publishes device presence, displays `AVAILABLE`, and waits for a
future rebuild.

A former holder returning after replacement also remains `AVAILABLE`; there is
no “original wins” behavior. If it should be selected again, the admin starts
another full rebuild.

## MQTT protocol

All JSON records are versioned. Device IDs are uppercase MAC addresses without
colons.

### Roster control

Topic:

```text
cube/roster/control
```

Retained rebuilding payload:

```json
{
  "protocol": 1,
  "state": "rebuilding",
  "rebuild_id": "server-generated-rebuild",
  "previous_epoch": "previous-active-epoch",
  "cube_count": 6,
  "excluded_device_ids": [],
  "started_at_ms": 0
}
```

Retained active payload:

```json
{
  "protocol": 1,
  "state": "active",
  "rebuild_id": "server-generated-rebuild",
  "epoch": "new-active-epoch",
  "cube_count": 6
}
```

This record is the rebuild barrier and final global commit. A server process
restart alone never changes it.

### Administrative reboot request

Topic:

```text
cube/device/all/reboot
```

Non-retained payload:

```json
{
  "protocol": 1,
  "rebuild_id": "server-generated-rebuild"
}
```

The retained control record, not delivery of this best-effort request, is
authoritative. Sleeping devices enroll when they observe the control record on
timer wake.

### Presence and heartbeat

```text
cube/device/{device_id}/presence
cube/device/{device_id}/heartbeat
```

Presence is retained metadata with `online`, tag, hardware class, preferred
set, firmware, last slot, and current `boot_id`. It has a retained offline last
will.

Heartbeat is non-retained and contains:

```json
{
  "protocol": 1,
  "boot_id": "random-per-boot",
  "rebuild_id": "current-rebuild-or-null",
  "sequence": 42
}
```

Only a live, newer sequence matching current presence and rebuild establishes
enrollment freshness.

### Epoch manifest and lease records

```text
cube/roster/{epoch}/manifest
cube/roster/{epoch}/slot/{logical_slot}/owner
cube/roster/{epoch}/device/{device_id}/assignment
cube/roster/{epoch}/tag-alias/{tag_id}
cube/roster/{epoch}/lease/{lease_id}
```

The immutable manifest lists selected devices, tags, slots, hardware classes,
player sets, and lease IDs. Owner, assignment, alias, and lease records must
match it exactly. These prepared records have no effect while control remains
`REBUILDING`; the active control record is the final commit.

### Assignment acknowledgement

```text
cube/device/{device_id}/assignment-ack
```

Non-retained payload:

```json
{
  "protocol": 1,
  "state": "active",
  "boot_id": "random-per-boot",
  "rebuild_id": "server-generated-rebuild",
  "logical_slot": 4,
  "epoch": "active-epoch",
  "lease_id": "active-lease",
  "sequence": 7
}
```

The server accepts only a live acknowledgement matching current presence,
control, manifest, and lease.

### Device-scoped sensor state

```text
cube/device/{device_id}/sensor-state
```

Non-retained payload:

```json
{
  "protocol": 1,
  "boot_id": "random-per-boot",
  "rebuild_id": "server-generated-rebuild",
  "sequence": 93,
  "epoch": "active-epoch",
  "lease_id": "active-lease",
  "observed_tag": "A9121466080104E0",
  "hall_present": true
}
```

Firmware publishes immediately after activation, on physical-state changes,
after MQTT reconnect, and every two seconds while fully awake.

The server accepts a snapshot only when device, boot, rebuild, epoch, lease,
and sequence match the active roster. It maps the sender through its current
assignment and maps `observed_tag` through the active epoch alias registry.
The resulting logical topology exists inside game coordination, not as
game-authoritative retained `cube/right/{slot}` traffic.

Legacy retained `cube/right/#` values are cleared during rollout and ignored
by pooled game coordination. The compiled `KNOWN_TAGS` mapping is not used for
pooled logical neighbor identity.

## Firmware state machine

```text
BOOT
  -> DHCP + unique device MQTT identity
  -> read roster control

ENROLLING
  -> publish fresh boot/rebuild heartbeat
  -> no logical commands or game-authoritative sensors

AVAILABLE
  -> not selected; wait for a future rebuild

ACTIVE
  -> require active control + matching manifest/owner/assignment/alias/lease
  -> apply slot-specific rotation and subscribe to cube/{slot}/...
  -> publish assignment acknowledgement and device-scoped sensor snapshots

SLEEPING
  -> keep assignment; publish device-scoped sleep intent
  -> timer wake reads roster control

QUARANTINED
  -> stop logical subscriptions and active-provenance sensor publication
  -> enter ENROLLING only for an explicit rebuild
```

## State hydration

Most display commands already use retained `cube/{slot}/...` topics, so a
selected cube receives them when it activates.

Implementation must inventory letter, borders, highlight, lock, brightness,
sleep interval, and transient display modes. Anything not retained is replayed
after the exact assignment acknowledgement.

Sensor state is never inherited from the prior holder. The committed device
publishes a fresh snapshot for the startup topology barrier.

## Failure and recovery behavior

| Situation | Result |
|---|---|
| Active cube misses heartbeat | Health reports offline; roster does not change |
| Active cube intentionally sleeps | Lease remains assigned; status is sleeping |
| Active cube fails mid-game | Game is degraded/stopped until admin rebuild |
| Extra powers on during a game | It remains available |
| Former holder returns | It remains available |
| Admin rebuilds with 7 compatible devices for 6 slots | 6 selected, 1 available |
| Admin rebuilds with 13 compatible devices for 12 slots | 12 selected, 1 available |
| Wrong-size extra is online | It is ineligible for the missing slot |
| Too few compatible devices enroll | Rebuild stays paused and uncommitted |
| Server restarts while control is active | Reconstruct same roster; no selection |
| Server restarts while rebuilding | Resume or safely abort the same rebuild ID |
| Crash before active commit | Previous epoch remains recorded; gameplay stays paused |
| Crash after active commit | Reconstruct complete new epoch and topology barrier |
| Delayed old sensor message arrives | Reject by device/boot/rebuild/epoch/lease |
| Sleeping cube sees rebuild barrier | Stay awake and enroll |

### Rebuild recovery

If the server restarts while `REBUILDING`, it reads the rebuild ID and previous
epoch from control:

- if a complete prepared successor exists, finish the active commit;
- if preparation is incomplete, resume enrollment/preparation for the same
  rebuild ID;
- an explicit admin abort may restore active control to the unchanged previous
  epoch only if its required devices are healthy;
- never infer a new rebuild from server startup alone.

## Inventory and commissioning

The authoritative inventory records:

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

It validates unique device IDs and tags, known hardware classes, compatible
firmware builds, enough capacity for supported profiles, and at least one
extra of each size when replacement coverage is promised.

Candidate `80:F3:DA:54:53:B8` still needs its size, board profile, and attached
NFC tag verified. `CC:DB:A7:99:0F:E0` is recorded as unable to enter download
mode and must not count as available capacity.

## Implementation areas

### `cube-pn5180`

- Replace MAC-to-logical-ID boot identity with stable device identity.
- Add unique device MQTT identity, presence, fresh enrollment heartbeat, and
  offline last will.
- Implement retained rebuild-control handling in full boot and timer-wake
  paths.
- Migrate maintenance sleep/wake topics and client IDs from logical slot to
  device ID.
- Add assignment validation, acknowledgement, and NVS last-slot preference.
- Gate logical commands behind the committed lease.
- Publish raw device-scoped sensor snapshots with full provenance.
- Disable logical `cube/right` publication and compiled tag fallback in pooled
  mode.
- Use DHCP and mDNS for pooled devices.
- Display `ENROLLING`, `AVAILABLE`, active slot, `SLEEPING`, and quarantine
  diagnostics.

### `cubes`

- Add an administrator-triggered roster rebuild manager with an injectable
  clock and persisted rebuild state.
- Make cube count, hardware classes, exclusions, and enrollment timeout
  explicit inputs.
- Never mutate roster membership from heartbeat expiry.
- Build and validate one immutable epoch per rebuild.
- Implement the startup topology barrier and bulk graph installation without
  invoking normal per-edge gameplay callbacks.
- Validate device-scoped sensor provenance and map raw tags through the active
  alias registry.
- Remove `cube/right/#` as game-authoritative input.
- Rehydrate non-retained display state after acknowledgement.
- Expose active, enrolling, sleeping, available, missing, incompatible, and
  excluded devices in logs/admin UI.

### `pi-deploy`

- Install inventory and persist roster/rebuild state.
- Add `rebuild-cube-roster` with 6/12 profile, exclusions, and progress output.
- Update monitoring and OTA tools for device ID and mDNS.
- Clear legacy retained logical neighbor topics during cutover.
- Preserve retained control and epoch records across broker restarts.

## Test plan

### Server unit tests

- Heartbeat timeout never changes an active roster.
- Sleeping status never makes a slot available.
- A server restart never starts a rebuild.
- Only an explicit rebuild ID opens enrollment.
- Retained presence without a live boot/rebuild-matched heartbeat is
  ineligible.
- Six-cube rebuild selects exactly six from seven compatible devices.
- Twelve-cube rebuild selects exactly twelve from thirteen.
- Wrong-size and excluded devices are never selected.
- Deterministic ranking preserves compatible prior slot preferences.
- Incomplete capacity never commits a partial manifest.
- Prepared records cannot activate while control is `REBUILDING`.
- Active control cannot reference an incomplete or inconsistent epoch.
- Restart before and after commit recovers the correct rebuild state.
- Delayed sensor snapshots with wrong provenance are ignored.
- Legacy retained `cube/right/{slot}` input is ignored.
- Topology snapshots are staged without per-edge callbacks.
- Bulk graph installation produces no guesses, scoring, or ABC transitions.

### Firmware-native tests

- Full boot uses fresh device and rebuild identity.
- Active control and all assignment records must agree before activation.
- A last-slot NVS value is preference, never authority.
- Timer wake uses device-scoped client and topics.
- Timer wake under unchanged active control returns to sleep without changing
  assignment.
- Timer wake under a new rebuild barrier stays awake and enrolls.
- Sleeping intent includes exact epoch, lease, and next wake.
- Available and quarantined devices cannot use logical command topics.
- Sensor snapshots contain exact boot, rebuild, epoch, lease, and sequence.
- Activation and physical-state changes immediately republish sensor state.

### Broker integration tests

- Leave active cubes idle beyond the 10-minute sleep threshold and verify no
  roster change.
- Observe repeated 20-second timer wakes and verify device-scoped sleep status.
- Start a rebuild while cubes sleep; verify all enroll by the 30-second window
  or remain explicitly missing without partial commit.
- Replace one cube in six-cube and twelve-cube profiles.
- Reconnect the old holder after commit and verify it remains available.
- Interrupt rebuild after every preparation step and verify no partial roster
  or topology becomes active.
- Restart broker and server independently during active and rebuilding states.
- Deliver retained presence and delayed old sensor messages; verify neither
  changes selection or topology.
- Keep a passive tag physically present while its device is omitted from the
  new roster; verify the staged topology excludes it.
- Prove a rebuild cannot trigger guesses, scoring, or ABC transitions from
  intermediate topology states.

### Hardware acceptance

- Rebuild six-cube single-player with a compatible extra.
- Rebuild twelve-cube two-player with compatible extras.
- Test small-to-small and large-to-large replacement.
- Confirm cross-size assignment is impossible.
- Verify player grouping and rotation after cold rebuild.
- Form words with the replacement on both left and right edges.
- Exercise MQTT-triggered and physical all-cube reboot procedures.
- Exercise rebuild while cubes are awake and while they are sleeping.
- Acceptance target: complete roster and topology ready within 45 seconds of
  starting the administrative rebuild.

## Rollout

1. Create and validate authoritative inventory.
2. Deploy device identity, presence, and sleep status in observe-only mode.
3. Deploy retained roster-control handling to full boot and timer-wake paths.
4. Deploy assignment gating and device-scoped sensor publication to every
   field cube.
5. Seed last-slot preferences from current IDs.
6. Add the server-side rebuild manager and startup topology barrier.
7. Stop consuming `cube/right/#` and clear its retained legacy values.
8. Switch pooled cubes to DHCP and update maintenance tooling.
9. Enable six-cube administrative rebuilds.
10. Verify player grouping, then enable twelve-cube rebuilds.
11. Commission at least one compatible extra per deployed size.

Mixed old/new firmware is unsafe once pooled assignment starts. An old cube
does not understand the rebuild barrier, lease gating, or device-scoped sensor
protocol.

## Open decisions before implementation

1. Where should inventory and persisted roster/rebuild state live?
2. How will the admin select cube count, hardware profile, and excluded device
   IDs?
3. Confirm actual small/large layout and `preferred_set` values.
4. Confirm whether 30 seconds covers timer wake plus Wi-Fi/MQTT startup on
   field batteries.
5. Define admin abort behavior when the previous roster is no longer healthy.
6. Inventory non-retained display state requiring replay.
7. Confirm DHCP capacity and mDNS support on the field router.
8. Verify size, board profile, RGB order, Hall profile, and NFC tag of each
   extra candidate.
