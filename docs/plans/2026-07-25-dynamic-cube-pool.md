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

The server first normalizes the requested slot/class profile, exclusions, and
effective enrollment deadline into an immutable retained rebuild-request
record. It then publishes a compact retained `REBUILDING` control record
containing a new rebuild ID and the request record's SHA-256 digest. The
request is server-only; firmware receives the bounded control record. The
server publishes the request at QoS 1 and verifies its retained canonical bytes
and digest before publishing the barrier, so control never intentionally
references an unconfirmed request.

Every device subscribes to roster control regardless of whether it is active
or available. Firmware persists `last_enrolled_rebuild_id` and
`pending_reboot_rebuild_id` in NVS. When any full or maintenance client
observes `REBUILDING` with an ID different from
`last_enrolled_rebuild_id`, it persists that ID as the pending reboot target
and triggers a full reboot without entering `ENROLLING`.

On the next boot, firmware reads roster control before completing enrollment.
If retained control matches `pending_reboot_rebuild_id`, that boot satisfies
the barrier: firmware generates the new `boot_id`, atomically promotes the
pending ID to `last_enrolled_rebuild_id`, clears the pending field, and enters
`ENROLLING`. A power loss after persisting the pending ID also supplies the
required full boot. Repeated retained deliveries or reconnects after promotion
enter `ENROLLING` without rebooting again. Firmware never enrolls from the
pre-barrier runtime.

An awake device therefore does not depend on receiving the non-retained
broadcast reboot request. A sleeping cube sees the retained barrier on its next
timer wake and takes the same persisted, exactly-once reboot path instead of
returning to sleep. The broadcast only reduces the time before awake devices
process the barrier.

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
never enrollment proof. While `ENROLLING`, firmware republishes the heartbeat
immediately after every MQTT reconnect and every two seconds. Its heartbeat
sequence is monotonic within the boot, allowing a restarted server to obtain
fresh enrollment proof without rebooting devices or starting a new rebuild.

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

1. publish the server-only epoch manifest;
2. publish server-side owner and tag-alias records plus one bounded membership
   record for every selected device;
3. verify the manifest digest, memberships, and all server-side records reached
   the broker at QoS 1;
4. publish compact roster control with `state: "ACTIVE"`, the new epoch, and
   the manifest digest as the single global commit.

Before step 4, all devices remain `ENROLLING`. At step 4, a selected device
activates only if its bounded membership record matches its device identity,
observed tag and hardware class, rebuild ID, control epoch, and committed
manifest digest. Firmware never downloads the aggregate manifest. Unselected
devices become `AVAILABLE`.

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
partially rebuilt graph. Selected active devices republish assignment
acknowledgements immediately after every MQTT reconnect and every two seconds
until the startup topology barrier closes. They continue periodic
acknowledgements while awake so a restarted server can reconstruct activation
proof before accepting sensor snapshots.

## Sleep lifecycle

Roster membership is independent of liveness after commit. Heartbeat expiry
updates health status but never changes membership, owner, alias, lease, or
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
  "applied_intent_id": "server-generated-intent",
  "next_wake_ms": 20000
}
```

The active lease remains valid while the device sleeps. The server reports the
device as `SLEEPING`, not failed or available.

On timer wake, the minimal maintenance client:

- uses the stable device ID rather than the logical slot as its MQTT client and
  topic identity;
- reads retained roster control and its retained device power intent;
- if control is still `ACTIVE` for its epoch and a matching intent requires
  `AWAKE`, starts the full client and remains awake;
- if control is still `ACTIVE` for its epoch and a matching intent allows
  sleep, publishes updated device-scoped sleep status and may return to sleep;
- if control is `REBUILDING` with a new rebuild ID, stays awake, performs the
  full enrollment boot, and does not return to sleep;
- never publishes the legacy logical `/status` keep-alive.

Device-scoped retained power intents replace logical auto-sleep flags for the
maintenance path. The server publishes `AWAKE` for every selected device
before starting or resuming a game and waits for fresh presence,
acknowledgement, and sensor state. A sleeping device observes that intent on
its next timer wake, so waking an unchanged roster requires neither a rebuild
nor physical access. `SLEEP_ALLOWED` permits the normal inactivity timer; it
does not force immediate sleep. Logical game commands remain lease-gated while
the cube is fully awake.

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
colons. Wire encodings are bounded: device IDs are 12 hex characters;
rebuild, epoch, lease, and intent IDs are 32 hex characters; SHA-256 digests
are 64 hex characters; NFC tag IDs are at most 32 hex characters; and hardware
class names are an enumerated maximum of 16 printable ASCII characters. These
limits, not the shorter descriptive placeholders in examples, determine packet
tests.

### Immutable rebuild request

Topic:

```text
cube/roster/rebuild/{rebuild_id}/request
```

This retained, server-only record contains the complete normalized request:

```json
{
  "protocol": 1,
  "rebuild_id": "server-generated-rebuild",
  "previous_epoch": "previous-active-epoch",
  "slots": [
    {"logical_slot": 1, "hardware_class": "small", "player_set": 0}
  ],
  "excluded_device_ids": [],
  "started_at_ms": 0,
  "enrollment_deadline_ms": 30000
}
```

The real `slots` array contains exactly 6 or 12 entries. The effective deadline
is resolved before publication, so recovery never depends on a default or
timeout override that exists only in process memory. The server computes
SHA-256 over canonical JSON and stores the digest with its persisted rebuild
state. A record is accepted only when its topic rebuild ID, payload rebuild ID,
and digest all agree. It is immutable for the lifetime of the rebuild.

Firmware does not subscribe to rebuild-request topics. After a restart, the
server reloads the request named by compact roster control, verifies its digest,
and uses the exact slots, classes, exclusions, and deadline to resume
enrollment or validate a prepared successor.

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
  "rebuild_id": "16-byte-hex-id",
  "request_digest": "32-byte-hex-sha256"
}
```

Retained active payload:

```json
{
  "protocol": 1,
  "state": "active",
  "rebuild_id": "16-byte-hex-id",
  "epoch": "16-byte-hex-id",
  "manifest_digest": "32-byte-hex-sha256"
}
```

This record is the rebuild barrier and final global commit. A server process
restart alone never changes it. Wire JSON is minified. IDs have fixed encoded
lengths, and the serialized control payload is capped at 384 bytes.

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
authoritative. Processing a new retained rebuild ID persists the ID and causes
exactly one full reboot even if this request is dropped. Duplicate broadcasts,
retained redelivery, and MQTT reconnects cannot cause additional reboots for
the same rebuild ID.

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
enrollment freshness. An enrolling device publishes immediately after boot,
after each MQTT reconnect, and every two seconds. `sequence` is a monotonic
per-boot heartbeat sequence and is independent from acknowledgement and sensor
sequences. The same heartbeat continues every two seconds while a device is
fully awake in `ACTIVE` or `AVAILABLE`, so heartbeat expiry has an actual
producer and is a health signal in those states. A device that has published
matching sleep state is instead monitored against its declared next-wake
deadline plus grace; missing awake heartbeats never changes roster membership.

### Server manifest and bounded device membership

```text
cube/roster/{epoch}/manifest
cube/roster/{epoch}/slot/{logical_slot}/owner
cube/roster/{epoch}/tag-alias/{tag_id}
cube/roster/{epoch}/device/{device_id}/membership
```

The immutable manifest lists selected devices, tags, slots, hardware classes,
player sets, and lease IDs. It and the owner/alias indexes are server-only and
may exceed the firmware packet budget. Their canonical JSON must match the
`manifest_digest`.

Each selected firmware client subscribes only to its own retained membership:

```json
{
  "protocol": 1,
  "device_id": "AABBCCDDEEFF",
  "rebuild_id": "16-byte-hex-id",
  "epoch": "16-byte-hex-id",
  "manifest_digest": "32-byte-hex-sha256",
  "logical_slot": 4,
  "hardware_class": "small",
  "tag_id": "16-byte-hex-tag",
  "lease_id": "16-byte-hex-id"
}
```

The membership is the firmware's complete activation input and is capped at
384 bytes of minified JSON. It must agree with the server-only manifest and
indexes. Prepared records have no effect while control remains `REBUILDING`;
active control naming the same epoch and manifest digest is the final commit.

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
control, manifest, membership, and lease. An active device publishes
immediately on activation, after each MQTT reconnect, and every two seconds
while fully awake. `sequence` is a monotonic per-boot acknowledgement sequence,
independent from heartbeat and sensor sequences. This replay makes activation
proof recoverable if the server restarts after commit but before installing
topology.

### Firmware MQTT packet budget

The checked-in PubSubClient defaults to a 256-byte packet buffer, which is not
large enough for the bounded protocol records once MQTT topic and fixed-header
bytes are included. Both the full firmware client and timer-wake maintenance
client must call `EspMQTTClient::setMaxPacketSize(512)` before connecting and
fail closed if allocation fails.

Firmware-consumed topics are capped at 80 UTF-8 bytes and minified payloads at
384 bytes. Including the MQTT PUBLISH fixed header and two-byte topic length,
every supported inbound packet must fit within 512 bytes. Aggregate rebuild
requests, manifests, and server indexes are published on topics firmware never
subscribes to. CI serializes worst-case 12-cube data with maximum-width IDs,
classes, and timestamps, asserts the firmware-bound packet sizes, and exercises
both clients under heap instrumentation. Hardware acceptance verifies the
512-byte allocation leaves the agreed minimum free-heap margin during Wi-Fi,
TLS-free MQTT, NFC polling, rendering, and timer-wake operation.

### Device power intent

```text
cube/device/{device_id}/power-intent
```

Retained payload:

```json
{
  "protocol": 1,
  "intent_id": "server-generated-intent",
  "desired_state": "awake",
  "epoch": "active-epoch",
  "lease_id": "active-lease",
  "issued_at_ms": 0
}
```

`desired_state` is `awake` or `sleep_allowed`. A device applies an intent only
when its `epoch` and `lease_id` exactly match the current active control and
assignment. It ignores an intent from an old epoch or lease. During
`REBUILDING`, the rebuild barrier takes precedence over every power intent.
When a new epoch commits, the server publishes a new intent for each selected
device; retained intents for devices omitted from the roster are harmless
because their leases no longer match and may be cleared during garbage
collection.

Both the full and maintenance clients subscribe using device identity. A
sleeping device checks the retained intent on every timer wake. It echoes the
applied `intent_id` in sleep state, and after an `awake` intent it starts the
full client and publishes fresh presence, assignment acknowledgement, and
sensor state. If a matching intent is missing, malformed, or stale, the device
uses `sleep_allowed` while `ACTIVE`; it never treats that condition as
authority to change roster membership.

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
  -> replay heartbeat on reconnect and every two seconds
  -> no logical commands or game-authoritative sensors

AVAILABLE
  -> not selected; wait for a future rebuild

ACTIVE
  -> require active control + matching bounded device membership
  -> apply slot-specific rotation and subscribe to cube/{slot}/...
  -> replay heartbeat, assignment acknowledgement, and sensor snapshots
  -> obey only matching retained device power intent

SLEEPING
  -> keep assignment; publish device-scoped sleep state
  -> timer wake reads roster control and device power intent

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
| Awake cube misses reboot broadcast | Retained rebuild barrier causes its one reboot |
| Server restarts during enrollment | Periodic heartbeats restore enrollment proof |
| Server restarts after active commit | Periodic acknowledgements restore activation proof |
| Sleeping active roster must start a game | Matching retained awake intent wakes it |

### Rebuild recovery

If the server restarts while `REBUILDING`, it reads the rebuild ID and request
digest from control, loads the immutable request, verifies the digest, and
recovers the previous epoch, normalized profile, exclusions, and effective
deadline from that request:

- if a complete prepared successor exists, finish the active commit;
- if preparation is incomplete, resume enrollment/preparation for the same
  rebuild ID;
- if the request is missing or its digest does not match, stay paused and
  require explicit administrative recovery rather than guessing a profile;
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
- Implement retained rebuild-control handling and persisted exactly-once
  per-rebuild reboot state in full boot and timer-wake paths.
- Set and verify a 512-byte MQTT buffer in both full and timer-wake clients;
  refuse pooled operation if allocation fails.
- Migrate maintenance sleep/wake topics and client IDs from logical slot to
  device ID.
- Add bounded membership validation, replayed acknowledgement, retained power
  intent, and NVS last-slot preference.
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
- Persist the normalized immutable rebuild request and canonical digest.
- Never mutate roster membership from heartbeat expiry.
- Build and validate one server-only immutable manifest plus bounded device
  memberships per rebuild.
- Implement the startup topology barrier and bulk graph installation without
  invoking normal per-edge gameplay callbacks.
- Validate device-scoped sensor provenance and map raw tags through the active
  alias registry.
- Remove `cube/right/#` as game-authoritative input.
- Rehydrate non-retained display state after acknowledgement.
- Publish epoch- and lease-scoped retained power intents and wait for selected
  devices to wake before enabling gameplay.
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
- Restart recovery rejects a missing or digest-mismatched immutable request.
- A mixed-size 12-cube request recovers the exact per-slot classes, exclusions,
  and effective deadline after a server restart.
- Restarted server receives periodic enrollment proof without rebooting cubes.
- Retained presence without a live boot/rebuild-matched heartbeat is
  ineligible.
- Six-cube rebuild selects exactly six from seven compatible devices.
- Twelve-cube rebuild selects exactly twelve from thirteen.
- Wrong-size and excluded devices are never selected.
- Deterministic ranking preserves compatible prior slot preferences.
- Incomplete capacity never commits a partial manifest.
- Prepared records cannot activate while control is `REBUILDING`.
- Active control cannot reference an incomplete or inconsistent epoch.
- Active control and every membership bind to the same manifest digest.
- Restart before and after commit recovers the correct rebuild state.
- Restart after active commit but before topology installation recovers
  periodic acknowledgements and completes the topology barrier.
- Delayed sensor snapshots with wrong provenance are ignored.
- Legacy retained `cube/right/{slot}` input is ignored.
- Topology snapshots are staged without per-edge callbacks.
- Bulk graph installation produces no guesses, scoring, or ABC transitions.

### Firmware-native tests

- Full boot uses fresh device and rebuild identity.
- A new retained rebuild ID persists before causing exactly one full reboot.
- Duplicate retained delivery, reconnect, and reboot broadcast do not cause a
  second reboot for the same rebuild ID.
- Active control and the bounded device membership must agree before
  activation.
- A last-slot NVS value is preference, never authority.
- Timer wake uses device-scoped client and topics.
- Timer wake under unchanged active control returns to sleep without changing
  assignment.
- Timer wake under a new rebuild barrier stays awake and enrolls.
- Enrolling heartbeat replays on reconnect and periodically with a monotonic
  per-boot sequence.
- Fully awake `ACTIVE` and `AVAILABLE` devices continue heartbeat replay every
  two seconds; sleep state switches health monitoring to the wake deadline.
- Active assignment acknowledgement replays on reconnect and periodically
  with a monotonic per-boot sequence.
- Both firmware clients successfully allocate a 512-byte MQTT buffer before
  connecting and fail closed when allocation is rejected.
- Worst-case control, membership, power-intent, and command PUBLISH packets
  remain within 512 bytes including topic and MQTT headers.
- Firmware never subscribes to aggregate request, manifest, owner, or alias
  topics.
- Timer wake applies only a power intent matching the active epoch and lease.
- An `awake` intent starts the full client; `sleep_allowed` permits the normal
  inactivity timer.
- Stale, missing, and malformed power intents cannot wake an old lease or
  change roster membership.
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
- Drop the administrative reboot broadcast entirely and verify every cube
  observes retained control, reboots exactly once, and enrolls.
- Restart the server after enrollment heartbeats and verify periodic replay
  completes selection without another cube reboot.
- Restart the server after active commit but before topology installation and
  verify acknowledgement and sensor replay complete the barrier.
- Put the unchanged active roster to sleep, publish matching retained `awake`
  intents, and verify all selected cubes wake on their next timer cycle without
  a rebuild or physical action.
- Deliver old-epoch and wrong-lease power intents and verify they are ignored.
- Replace one cube in six-cube and twelve-cube profiles.
- Reconnect the old holder after commit and verify it remains available.
- Interrupt rebuild after every preparation step and verify no partial roster
  or topology becomes active.
- Restart broker and server independently during active and rebuilding states.
- Restart during a mixed-size rebuild and verify recovery uses the original
  slot/class profile, exclusions, and effective deadline.
- Run a maximum-width 12-cube rebuild and verify both full and maintenance
  clients receive every subscribed record without truncation or disconnect.
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
- Measure minimum free heap with a 512-byte MQTT buffer during full-client and
  timer-wake operation; enforce the agreed safety margin.
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
