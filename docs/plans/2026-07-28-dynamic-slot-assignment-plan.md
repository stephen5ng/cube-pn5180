# Dynamic Slot Assignment Implementation Plan (Rollout Steps 2 + 3)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make a cube's logical slot a runtime value the server assigns — the server publishes a durable, per-device retained assignment record, and firmware takes its slot from that record instead of the compiled `cube_id` — while preserving today's behavior end to end (the authority cutover, console, and game-start gate are the *next* plan).

**Architecture:** Two phases in two independent git repos. **Phase A (`cubes`, rollout step 2)** adds a persisted `MAC → {slot, generation, tag, hardware_class}` roster whose on-disk file is the source of truth, seeded to match today's assignments, and publishes one retained `cube/assign/{MAC}` record per device at startup. It changes no game behavior. **Phase B (`cube-pn5180`, rollout step 3)** makes firmware read that record at boot, persist the applied `{slot, generation}` in NVS, fall back to the compiled `cube_id` while the authority marker has never been latched, publish MAC-scoped presence with an offline last will, answer MAC-scoped liveness challenges, and move the deep-sleep keepalive path off slot-scoped identity. Phase A is shippable and reviewable on its own.

**Tech Stack:** Python 3 + pytest + aiomqtt (`cubes`); C++ (Arduino/PlatformIO, ESP32), Unity native unit tests, vendored `lib/EspMQTTClient` and `lib/PubSubClient`, ESP32 `Preferences` (NVS) (`cube-pn5180`).

## Global Constraints

- **Execution gate — do this first:** `docs/plans/2026-07-26-cube-identity-foundation-plan.md` (rollout step 1) is **not yet implemented** and Phase B depends on it. Execute its Tasks 1–4 before starting Phase B. Phase B *consumes* `findCubeIpOctet(const char*)` and `String makeMqttClientId(const String& mac, const char* suffix)` and must not redefine them. Phase A has no such dependency and may be executed first or in parallel.
- **Two separate git repos.** Every task below is tagged with its repo. `cd` into that repo before running any command or `git` operation in the task. Never stage a file from the other repo.
  - `cubes` repo: `/Users/stephenng/programming/blockwords/cubes`
  - `cube-pn5180` repo: `/Users/stephenng/programming/blockwords/cube-pn5180`
- **`cubes` repo test commands** (from that repo's `CLAUDE.md`, verbatim): run ALL integration tests before EVERY commit — `pytest tests/integration/ -v`; never commit with failing tests. Run unit tests before committing. Run the functional tests (`scripts/run_functional_tests.sh`) before committing feature changes. The tracked pre-commit hook runs tests via `cube_env/bin/python3`, not whatever `pytest` is on PATH.
- **`cube-pn5180` repo test commands** (from that repo's `CLAUDE.md`): native tests FIRST — `~/.platformio/penv/bin/platformio test -e native` — then hardware compile — `~/.platformio/penv/bin/platformio run -e v1 -e v6 -e v6_with_hall -e v6_with_hall_analog -e v6_with_hall_neighbor` (these are the five real environments in `platformio.ini`; `esp32dev` is the `board` value, not an environment).
- **Native tests compile only `cube_utilities.cpp` and `cube_tags.cpp`** (`platformio.ini:76-79` explicitly excludes `main.cpp`). All natively testable logic MUST live in `cube_utilities.cpp`. Arduino-only code (NVS, MQTT) goes in a new `src/cube_slot_store.cpp`, which the native filter excludes automatically because that filter is an explicit include list.
- **No JSON library is available on the ESP32** (`lib_deps` has none, and none is added here). Assignment records are parsed by a hand-rolled parser in `cube_utilities.cpp` and built with `snprintf`. The parser MUST distinguish `"slot": null` (unassigned) from malformed (fail closed) — the design treats those differently.
- **This plan does NOT publish `cube/roster/authoritative`.** The authority marker stays off through both phases, so firmware keeps falling back to the compiled `cube_id` when no record is present. That cutover is rollout step 4.
- **Behavior preservation is the acceptance bar for both phases.** With a roster seeded to today's assignments, every cube must end up on exactly the slot it uses today, and the game must play identically.
- **MAC normalization is uppercase with colons stripped** everywhere (`CC:DB:A7:9F:C2:84` → `CCDBA79FC284`), matching `removeColonsFromMac`. Topic segments use the normalized form.
- **Protocol version is `1`** in every record. A record whose `protocol` is absent or not `1` is malformed.

## Decisions this plan makes (answering the design doc's open questions)

- **Open decision #1 — roster file location and durable write:** the roster lives at `src/data/cube_roster.json` inside the `cubes` repo, overridable with the `LEXACUBE_ROSTER_PATH` environment variable. Writes are a single-file atomic replace: write to a sibling temp file, `flush()` + `os.fsync()`, then `os.replace()`. Moving the file to a Pi-managed location outside the repo is `pi-deploy` work in rollout step 4.
- **Open decision #5 — NVS namespace and keys:** namespace `cubepool`; keys `slot` (int32, `-1` = unassigned), `gen` (uint32), `auth` (uint8 authority latch). This namespace is new and does not touch the existing sleep-state persistence, which uses RTC memory (`pin0_state_at_sleep`), not NVS.
- **Slot changes take effect via reboot.** If an assignment record arrives whose slot differs from the slot already applied this boot, firmware writes the new `{slot, generation}` to NVS and calls `ESP.restart()`. This matches the design's field workflow (assign, then reboot) and avoids unsubscribing ~20 slot-scoped topics at runtime. Writing NVS *before* restarting is what makes it converge in one reboot instead of looping.
- **`auto_sleep` and `status` are dual-scoped, not moved.** The design's rollout promises steps 1–3 preserve behavior, and the only slot-scoped publisher of `auto_sleep` is `cube-pn5180/tools/wake.sh:8,13`, which has no MAC map (that map arrives with the console in step 4). So firmware subscribes to *both* `cube/{slot}/auto_sleep` and `cube/device/{MAC}/auto_sleep`, and publishes status and the auto-sleep flag to both. Step 4 retires the slot-scoped pair once tooling is inventory-aware.
- **`configurePins(cube_id)` needs no change.** It was inspected (`src/main.cpp:44-60`): the pin values are chosen entirely by the `BOARD_V6` macro and `cube_id` is used only in log strings. It is not a slot-dependent behavior.
- **`DisplayManager` keeps its early construction** (`src/main.cpp:1554`, before any assignment can arrive) and gains a `setSlotRotation(int)` setter, because the constructor uses `cube_id` for exactly one thing: `rotation = (cube_id_int <= 6) ? 2 : 0` (`src/main.cpp:369`).
- **Deep sleep must disconnect explicitly.** The design doc claims "the deep-sleep path disconnects cleanly (no last will fires)". That is **false against the current code**: `enterSleepMode()` (`src/main.cpp:958-999`) calls `esp_deep_sleep_start()` with no MQTT disconnect, so the broker sees an ungraceful drop and the retained last will would overwrite the `sleeping` presence record with `offline`. Task B3 adds an explicit disconnect before sleeping. The vendored `lib/EspMQTTClient` exposes no `disconnect()`, so Task B3 adds one to the vendored header.

## Assumptions to confirm (do not block on these)

- **MAC↔tag pairing.** The seed roster in Task A2 pairs each MAC with a tag by slot, using `cube-pn5180/src/cube_tags.cpp` (primary set rows 1–12, backup set rows 13–24) and the production MAC table. This is only correct while each ESP32 chip stays in the enclosure whose tag it is paired with. Chips *have* been moved between enclosures historically. Task A2 includes a printed pairing review step; correct any row that does not match the physical cube. The tag inventory is unused until rollout step 5, so an error here is not shippable-blocking.
- **`hardware_class`.** Every seeded device gets `"standard"`. The design's small/large distinction is only enforced by the step-4 console; the field is recorded now so step 4 has it. Update values if a second size exists.

---

# Phase A — `cubes` server: persisted roster and retained assignment records

**Repo:** `/Users/stephenng/programming/blockwords/cubes`

---

### Task A1: Roster model with durable atomic persistence

**Repo:** `cubes` — run every command from `/Users/stephenng/programming/blockwords/cubes`.

**Files:**
- Create: `src/hardware/cube_roster.py`
- Test: `tests/test_cube_roster.py`

**Interfaces:**
- Consumes: nothing.
- Produces:
  - `normalize_mac(mac: str) -> str` — uppercase, colons stripped; raises `ValueError` for anything that is not 12 hex digits.
  - `@dataclass(frozen=True) DeviceRecord` with fields `mac: str`, `slot: int | None`, `generation: int`, `tag: str | None`, `hardware_class: str`.
  - `class CubeRoster` with `CubeRoster.load(path: Path) -> CubeRoster`, `save(path: Path) -> None`, `devices() -> list[DeviceRecord]` (sorted by MAC), `get(mac: str) -> DeviceRecord | None`, `mac_for_slot(slot: int) -> str | None`, `assign_slot(slot: int, mac: str) -> None`, `set_tag(mac: str, tag: str) -> None`, `assignment_payload(mac: str) -> str`.
  - `ROSTER_PATH_ENV = "LEXACUBE_ROSTER_PATH"`, `default_roster_path() -> Path`.

- [ ] **Step 1: Write the failing tests**

Create `tests/test_cube_roster.py`:

```python
import json
import pytest
from pathlib import Path

from hardware.cube_roster import (
    CubeRoster,
    DeviceRecord,
    normalize_mac,
)

PRIMARY = "CCDBA79FC284"
BACKUP = "80F3DA5453B8"


def _roster(tmp_path: Path) -> Path:
    path = tmp_path / "cube_roster.json"
    path.write_text(json.dumps({
        "protocol": 1,
        "devices": {
            PRIMARY: {"slot": 1, "generation": 1, "tag": "A9121466080104E0",
                      "hardware_class": "standard"},
            BACKUP: {"slot": None, "generation": 1, "tag": "C6EDD203530104E0",
                     "hardware_class": "standard"},
        },
    }))
    return path


def test_normalize_mac_strips_colons_and_uppercases():
    assert normalize_mac("cc:db:a7:9f:c2:84") == PRIMARY
    assert normalize_mac(PRIMARY) == PRIMARY


def test_normalize_mac_rejects_garbage():
    with pytest.raises(ValueError):
        normalize_mac("not-a-mac")
    with pytest.raises(ValueError):
        normalize_mac("CC:DB:A7:9F:C2")


def test_load_returns_devices_sorted_by_mac(tmp_path):
    roster = CubeRoster.load(_roster(tmp_path))
    assert [d.mac for d in roster.devices()] == [BACKUP, PRIMARY]
    assert roster.get(PRIMARY) == DeviceRecord(
        mac=PRIMARY, slot=1, generation=1,
        tag="A9121466080104E0", hardware_class="standard")


def test_mac_for_slot(tmp_path):
    roster = CubeRoster.load(_roster(tmp_path))
    assert roster.mac_for_slot(1) == PRIMARY
    assert roster.mac_for_slot(4) is None


def test_assign_slot_moves_slot_and_bumps_only_the_two_devices(tmp_path):
    roster = CubeRoster.load(_roster(tmp_path))
    roster.assign_slot(1, BACKUP)

    assert roster.get(PRIMARY).slot is None
    assert roster.get(PRIMARY).generation == 2
    assert roster.get(BACKUP).slot == 1
    assert roster.get(BACKUP).generation == 2
    assert roster.mac_for_slot(1) == BACKUP


def test_assign_slot_leaves_unaffected_devices_untouched(tmp_path):
    path = tmp_path / "r.json"
    path.write_text(json.dumps({
        "protocol": 1,
        "devices": {
            PRIMARY: {"slot": 1, "generation": 1, "tag": None, "hardware_class": "standard"},
            BACKUP: {"slot": None, "generation": 1, "tag": None, "hardware_class": "standard"},
            "AABBCCDDEEFF": {"slot": 2, "generation": 7, "tag": None, "hardware_class": "standard"},
        },
    }))
    roster = CubeRoster.load(path)
    roster.assign_slot(1, BACKUP)
    untouched = roster.get("AABBCCDDEEFF")
    assert untouched.slot == 2
    assert untouched.generation == 7


def test_assign_slot_is_idempotent(tmp_path):
    roster = CubeRoster.load(_roster(tmp_path))
    roster.assign_slot(1, PRIMARY)
    assert roster.get(PRIMARY).generation == 1


def test_assign_slot_rejects_unknown_mac(tmp_path):
    roster = CubeRoster.load(_roster(tmp_path))
    with pytest.raises(KeyError):
        roster.assign_slot(1, "010203040506")


def test_save_then_load_round_trips(tmp_path):
    path = _roster(tmp_path)
    roster = CubeRoster.load(path)
    roster.assign_slot(1, BACKUP)
    roster.save(path)

    reloaded = CubeRoster.load(path)
    assert reloaded.get(BACKUP).slot == 1
    assert reloaded.get(BACKUP).generation == 2
    assert reloaded.get(PRIMARY).slot is None


def test_save_leaves_no_temp_files_behind(tmp_path):
    path = _roster(tmp_path)
    roster = CubeRoster.load(path)
    roster.save(path)
    assert [p.name for p in tmp_path.iterdir()] == ["cube_roster.json"]


def test_set_tag(tmp_path):
    roster = CubeRoster.load(_roster(tmp_path))
    roster.set_tag(BACKUP, "0611F8B8500104E0")
    assert roster.get(BACKUP).tag == "0611F8B8500104E0"


def test_assignment_payload_for_assigned_device(tmp_path):
    roster = CubeRoster.load(_roster(tmp_path))
    assert json.loads(roster.assignment_payload(PRIMARY)) == {
        "protocol": 1, "generation": 1, "slot": 1}


def test_assignment_payload_for_unassigned_device(tmp_path):
    roster = CubeRoster.load(_roster(tmp_path))
    assert json.loads(roster.assignment_payload(BACKUP)) == {
        "protocol": 1, "generation": 1, "slot": None}


def test_load_rejects_unknown_protocol(tmp_path):
    path = tmp_path / "bad.json"
    path.write_text(json.dumps({"protocol": 99, "devices": {}}))
    with pytest.raises(ValueError):
        CubeRoster.load(path)


def test_load_rejects_two_macs_holding_the_same_slot(tmp_path):
    path = tmp_path / "dup.json"
    path.write_text(json.dumps({
        "protocol": 1,
        "devices": {
            PRIMARY: {"slot": 1, "generation": 1, "tag": None, "hardware_class": "standard"},
            BACKUP: {"slot": 1, "generation": 1, "tag": None, "hardware_class": "standard"},
        },
    }))
    with pytest.raises(ValueError):
        CubeRoster.load(path)
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cube_env/bin/python3 -m pytest tests/test_cube_roster.py -v`
Expected: FAIL — `ModuleNotFoundError: No module named 'hardware.cube_roster'`.

- [ ] **Step 3: Write the implementation**

Create `src/hardware/cube_roster.py`:

```python
"""Persisted MAC -> {slot, generation, tag, hardware_class} roster.

The on-disk file is the source of truth for which physical cube (MAC) holds
which logical slot; MQTT retained records are derived from it. Each device
carries its own generation, bumped only when that device's assignment changes,
so a one-slot swap leaves every other cube's record untouched.
"""
from __future__ import annotations

import json
import os
import re
from dataclasses import dataclass, replace
from pathlib import Path

PROTOCOL_VERSION = 1
ROSTER_PATH_ENV = "LEXACUBE_ROSTER_PATH"

_MAC_RE = re.compile(r"^[0-9A-F]{12}$")


def default_roster_path() -> Path:
    override = os.environ.get(ROSTER_PATH_ENV)
    if override:
        return Path(override)
    return Path(__file__).resolve().parent.parent / "data" / "cube_roster.json"


def normalize_mac(mac: str) -> str:
    """Uppercase, colon-free MAC. Raises ValueError if it is not 12 hex digits."""
    candidate = mac.replace(":", "").replace("-", "").strip().upper()
    if not _MAC_RE.match(candidate):
        raise ValueError(f"not a MAC address: {mac!r}")
    return candidate


@dataclass(frozen=True)
class DeviceRecord:
    mac: str
    slot: int | None
    generation: int
    tag: str | None
    hardware_class: str


class CubeRoster:
    def __init__(self, devices: dict[str, DeviceRecord]):
        self._devices = devices

    @classmethod
    def load(cls, path: Path) -> "CubeRoster":
        raw = json.loads(Path(path).read_text())
        if raw.get("protocol") != PROTOCOL_VERSION:
            raise ValueError(f"roster protocol {raw.get('protocol')!r} != {PROTOCOL_VERSION}")

        devices: dict[str, DeviceRecord] = {}
        slots_seen: dict[int, str] = {}
        for mac, entry in raw.get("devices", {}).items():
            key = normalize_mac(mac)
            slot = entry.get("slot")
            if slot is not None:
                if slot in slots_seen:
                    raise ValueError(
                        f"slot {slot} held by both {slots_seen[slot]} and {key}")
                slots_seen[slot] = key
            devices[key] = DeviceRecord(
                mac=key,
                slot=slot,
                generation=int(entry.get("generation", 1)),
                tag=entry.get("tag"),
                hardware_class=entry.get("hardware_class", "standard"),
            )
        return cls(devices)

    def save(self, path: Path) -> None:
        """Atomic single-file replace, so a crash mid-write cannot truncate it."""
        path = Path(path)
        payload = {
            "protocol": PROTOCOL_VERSION,
            "devices": {
                d.mac: {"slot": d.slot, "generation": d.generation,
                        "tag": d.tag, "hardware_class": d.hardware_class}
                for d in self.devices()
            },
        }
        tmp = path.with_name(path.name + ".tmp")
        with open(tmp, "w") as f:
            json.dump(payload, f, indent=2, sort_keys=True)
            f.write("\n")
            f.flush()
            os.fsync(f.fileno())
        os.replace(tmp, path)

    def devices(self) -> list[DeviceRecord]:
        return [self._devices[mac] for mac in sorted(self._devices)]

    def get(self, mac: str) -> DeviceRecord | None:
        return self._devices.get(normalize_mac(mac))

    def mac_for_slot(self, slot: int) -> str | None:
        for device in self.devices():
            if device.slot == slot:
                return device.mac
        return None

    def assign_slot(self, slot: int, mac: str) -> None:
        """Give slot to mac, clearing it from any prior holder.

        Bumps the generation of the two affected devices only. Assigning a slot
        to the MAC that already holds it is a no-op, so republishing a roster
        never invents a new generation.
        """
        key = normalize_mac(mac)
        if key not in self._devices:
            raise KeyError(f"unknown MAC {key}")
        if self._devices[key].slot == slot:
            return

        prior = self.mac_for_slot(slot)
        if prior is not None:
            self._devices[prior] = replace(
                self._devices[prior], slot=None,
                generation=self._devices[prior].generation + 1)
        self._devices[key] = replace(
            self._devices[key], slot=slot,
            generation=self._devices[key].generation + 1)

    def set_tag(self, mac: str, tag: str) -> None:
        key = normalize_mac(mac)
        if key not in self._devices:
            raise KeyError(f"unknown MAC {key}")
        self._devices[key] = replace(self._devices[key], tag=tag)

    def assignment_payload(self, mac: str) -> str:
        """The retained cube/assign/{MAC} record body."""
        device = self.get(mac)
        if device is None:
            raise KeyError(f"unknown MAC {mac}")
        return json.dumps({
            "protocol": PROTOCOL_VERSION,
            "generation": device.generation,
            "slot": device.slot,
        })
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `cube_env/bin/python3 -m pytest tests/test_cube_roster.py -v`
Expected: PASS — all 14 tests.

- [ ] **Step 5: Run the repo's required test suites**

Run: `cube_env/bin/python3 -m pytest tests/integration/ -v`
Expected: PASS (this task adds a module nothing imports yet; it must not disturb anything).

- [ ] **Step 6: Commit**

```bash
cd /Users/stephenng/programming/blockwords/cubes
git add src/hardware/cube_roster.py tests/test_cube_roster.py
git commit -m "feat: persisted MAC->slot cube roster with atomic writes"
```

---

### Task A2: Seed the roster and publish retained assignment records at startup

**Repo:** `cubes` — run every command from `/Users/stephenng/programming/blockwords/cubes`.

**Files:**
- Create: `src/data/cube_roster.json`
- Modify: `src/hardware/cube_roster.py` (add the publisher)
- Modify: `main.py:104-115` (wire it into startup)
- Test: `tests/test_cube_roster_publish.py`

**Interfaces:**
- Consumes: `CubeRoster`, `default_roster_path()` from Task A1.
- Produces: `async def publish_assignments(publish_queue, roster: CubeRoster, now_ms: int = 0) -> None` — enqueues one retained `cube/assign/{MAC}` message per device, in MAC order. Also `def load_roster_or_none(path=None) -> CubeRoster | None` — returns `None` (after logging a warning) when the file is missing or invalid, so a missing roster degrades to today's behavior rather than failing startup.

- [ ] **Step 1: Write the failing tests**

Create `tests/test_cube_roster_publish.py`:

```python
import asyncio
import json
import pytest
from pathlib import Path

from hardware.cube_roster import (
    CubeRoster,
    load_roster_or_none,
    publish_assignments,
)

PRIMARY = "CCDBA79FC284"
BACKUP = "80F3DA5453B8"


def _roster_file(tmp_path: Path) -> Path:
    path = tmp_path / "cube_roster.json"
    path.write_text(json.dumps({
        "protocol": 1,
        "devices": {
            PRIMARY: {"slot": 1, "generation": 1, "tag": None, "hardware_class": "standard"},
            BACKUP: {"slot": None, "generation": 3, "tag": None, "hardware_class": "standard"},
        },
    }))
    return path


def _drain(queue: asyncio.Queue) -> list[tuple]:
    items = []
    while not queue.empty():
        items.append(queue.get_nowait())
    return items


async def test_publish_assignments_emits_one_retained_record_per_device(tmp_path):
    roster = CubeRoster.load(_roster_file(tmp_path))
    queue: asyncio.Queue = asyncio.Queue()

    await publish_assignments(queue, roster, now_ms=0)

    published = _drain(queue)
    assert [(topic, retain) for topic, _, retain, _ in published] == [
        (f"cube/assign/{BACKUP}", True),
        (f"cube/assign/{PRIMARY}", True),
    ]
    payloads = {topic: json.loads(msg) for topic, msg, _, _ in published}
    assert payloads[f"cube/assign/{PRIMARY}"] == {
        "protocol": 1, "generation": 1, "slot": 1}
    assert payloads[f"cube/assign/{BACKUP}"] == {
        "protocol": 1, "generation": 3, "slot": None}


async def test_publish_assignments_is_idempotent_across_restarts(tmp_path):
    """Restart republishes every device at its stored generation, unchanged."""
    path = _roster_file(tmp_path)
    queue: asyncio.Queue = asyncio.Queue()

    await publish_assignments(queue, CubeRoster.load(path), now_ms=0)
    first = _drain(queue)
    await publish_assignments(queue, CubeRoster.load(path), now_ms=0)
    second = _drain(queue)

    assert first == second


async def test_publish_assignments_reflects_a_swap(tmp_path):
    path = _roster_file(tmp_path)
    roster = CubeRoster.load(path)
    roster.assign_slot(1, BACKUP)
    roster.save(path)

    queue: asyncio.Queue = asyncio.Queue()
    await publish_assignments(queue, CubeRoster.load(path), now_ms=0)
    payloads = {topic: json.loads(msg) for topic, msg, _, _ in _drain(queue)}

    assert payloads[f"cube/assign/{BACKUP}"] == {
        "protocol": 1, "generation": 4, "slot": 1}
    assert payloads[f"cube/assign/{PRIMARY}"] == {
        "protocol": 1, "generation": 2, "slot": None}


def test_load_roster_or_none_returns_none_when_missing(tmp_path):
    assert load_roster_or_none(tmp_path / "absent.json") is None


def test_load_roster_or_none_returns_none_when_invalid(tmp_path):
    path = tmp_path / "bad.json"
    path.write_text("{ not json")
    assert load_roster_or_none(path) is None


def test_shipped_seed_roster_is_loadable_and_covers_every_slot(monkeypatch):
    monkeypatch.delenv("LEXACUBE_ROSTER_PATH", raising=False)
    roster = load_roster_or_none()
    assert roster is not None
    for slot in [1, 2, 3, 4, 5, 6, 11, 12, 13, 14, 15, 16]:
        assert roster.mac_for_slot(slot) is not None, f"slot {slot} unseeded"
    assert len(roster.devices()) == 18
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cube_env/bin/python3 -m pytest tests/test_cube_roster_publish.py -v`
Expected: FAIL — `ImportError: cannot import name 'publish_assignments'`.

- [ ] **Step 3: Add the publisher and loader**

Append to `src/hardware/cube_roster.py` (and add `import logging` to its imports, with `logger = logging.getLogger(__name__)` below the constants):

```python
def load_roster_or_none(path: Path | None = None) -> "CubeRoster | None":
    """Load the roster, or None if it is missing or unreadable.

    A missing roster degrades to the pre-assignment behavior (firmware falls
    back to its compiled cube_id) instead of failing game startup.
    """
    target = Path(path) if path is not None else default_roster_path()
    try:
        return CubeRoster.load(target)
    except (OSError, ValueError, json.JSONDecodeError) as e:
        logger.warning(f"cube roster unavailable at {target}: {e}")
        return None


async def publish_assignments(publish_queue, roster: "CubeRoster", now_ms: int = 0) -> None:
    """Publish every device's retained assignment record at its stored generation.

    Idempotent: republishing an unchanged roster produces byte-identical records,
    so a server restart converges both halves of an interrupted swap.
    """
    for device in roster.devices():
        await publish_queue.put((
            f"cube/assign/{device.mac}",
            roster.assignment_payload(device.mac),
            True,
            now_ms,
        ))
```

- [ ] **Step 4: Create the seed roster**

Create `src/data/cube_roster.json`. Slots and MACs mirror the production `CUBE_MAC_TABLE` in `cube-pn5180/src/cube_utilities.cpp` (with the six backups from the step-1 plan) so this is behavior-preserving; tags come from `cube-pn5180/src/cube_tags.cpp` paired by slot (primary set for primaries, backup set for backups). Every generation starts at `1`.

```json
{
  "protocol": 1,
  "devices": {
    "048308596E74": {"slot": 13, "generation": 1, "tag": "BFBD1366080104E0", "hardware_class": "standard"},
    "048308597698": {"slot": 5, "generation": 1, "tag": "71E81366080104E0", "hardware_class": "standard"},
    "3C8A1F77DF8C": {"slot": 2, "generation": 1, "tag": "B1FD1366080104E0", "hardware_class": "standard"},
    "5C013B4A874C": {"slot": null, "generation": 1, "tag": "39ECD203530104E0", "hardware_class": "standard"},
    "5C013B64E284": {"slot": null, "generation": 1, "tag": "0A40D303530104E0", "hardware_class": "standard"},
    "5C013B65462C": {"slot": null, "generation": 1, "tag": "0611F8B8500104E0", "hardware_class": "standard"},
    "80F3DA5453B8": {"slot": null, "generation": 1, "tag": "C6EDD203530104E0", "hardware_class": "standard"},
    "8C4F00367A88": {"slot": 15, "generation": 1, "tag": "32961366080104E0", "hardware_class": "standard"},
    "8C4F00377CDC": {"slot": 3, "generation": 1, "tag": "30071466080104E0", "hardware_class": "standard"},
    "9454C5EE894C": {"slot": 14, "generation": 1, "tag": "6DB11366080104E0", "hardware_class": "standard"},
    "9454C5F1AF00": {"slot": 11, "generation": 1, "tag": "C1A81366080104E0", "hardware_class": "standard"},
    "CCDBA79B5D9C": {"slot": 4, "generation": 1, "tag": "BD291466080104E0", "hardware_class": "standard"},
    "CCDBA79FC284": {"slot": 1, "generation": 1, "tag": "A9121466080104E0", "hardware_class": "standard"},
    "D48AFC9FB0C0": {"slot": null, "generation": 1, "tag": "1942D303530104E0", "hardware_class": "standard"},
    "D8BC38E5A838": {"slot": null, "generation": 1, "tag": "B13FD303530104E0", "hardware_class": "standard"},
    "D8BC38F93930": {"slot": 16, "generation": 1, "tag": "FAADF7B8500104E0", "hardware_class": "standard"},
    "ECE334798ABC": {"slot": 6, "generation": 1, "tag": "361E1466080104E0", "hardware_class": "standard"},
    "ECE334799D2C": {"slot": 12, "generation": 1, "tag": "829E1366080104E0", "hardware_class": "standard"}
  }
}
```

The six `"slot": null` devices are the backups; they hold no slot today, exactly as the compiled table's shared `cube_id`s meant in practice.

- [ ] **Step 5: Review the MAC↔tag pairing with a human**

Print the pairing and confirm each row names the tag physically on the enclosure that currently houses that chip:

```bash
cube_env/bin/python3 -c "
from pathlib import Path; import sys; sys.path.insert(0, 'src')
from hardware.cube_roster import CubeRoster
for d in CubeRoster.load(Path('src/data/cube_roster.json')).devices():
    print(f'{d.mac}  slot={str(d.slot):>4}  tag={d.tag}')
"
```

Expected: 18 rows, 12 with a slot and 6 with `slot=None`. Report the table to the user and ask them to confirm the MAC↔tag pairs before continuing; correct any row they flag. The tag column is not consumed by any code until rollout step 5, so an unconfirmed pair does not block the rest of this plan — but do not silently skip the ask.

- [ ] **Step 6: Run the tests to verify they pass**

Run: `cube_env/bin/python3 -m pytest tests/test_cube_roster_publish.py tests/test_cube_roster.py -v`
Expected: PASS — including `test_shipped_seed_roster_is_loadable_and_covers_every_slot`.

- [ ] **Step 7: Wire publishing into game startup**

In `main.py`, add to the imports near the other `hardware` imports:

```python
from hardware.cube_roster import load_roster_or_none, publish_assignments
```

Then in `async def main(...)`, inside the `if not args.replay:` block that currently starts at `main.py:110` (the one with the `subscribe_client.subscribe("game/guess")` calls), add at the top of that block:

```python
                    roster = load_roster_or_none()
                    if roster is not None:
                        await publish_assignments(publish_queue, roster, 0)
```

It must be inside `if not args.replay:` so replayed runs produce no extra publishes and the functional goldens stay byte-identical.

- [ ] **Step 8: Run every required suite, including functional**

Run, in order:

```bash
cube_env/bin/python3 -m pytest tests/integration/ -v
scripts/run_unit_tests.sh
scripts/run_functional_tests.sh
```

Expected: PASS for all three, with **no golden diffs**. If a golden changed, the publish is escaping the `not args.replay` guard — fix that rather than re-baselining the golden.

- [ ] **Step 9: Verify against the real broker (manual)**

With the broker reachable, start the game normally and confirm the records land:

```bash
mosquitto_sub -h 192.168.8.247 -t 'cube/assign/#' -v -W 3
```

Expected: 18 retained lines, e.g. `cube/assign/CCDBA79FC284 {"protocol": 1, "generation": 1, "slot": 1}`. Cubes running today's firmware ignore them entirely — confirm the game still plays normally.

- [ ] **Step 10: Commit**

```bash
cd /Users/stephenng/programming/blockwords/cubes
git add src/data/cube_roster.json src/hardware/cube_roster.py main.py tests/test_cube_roster_publish.py
git commit -m "feat: seed cube roster and publish retained slot assignments"
```

---

# Phase B — `cube-pn5180` firmware: take the slot from the assignment record

**Repo:** `/Users/stephenng/programming/blockwords/cube-pn5180`
**Prerequisite:** the step-1 foundation plan's Tasks 1–4 are merged (`findCubeIpOctet`, `makeMqttClientId` exist), and Phase A is deployed so retained records exist on the broker.

---

### Task B1: Parse assignment records and resolve the slot (pure logic, native tests)

**Repo:** `cube-pn5180`.

**Files:**
- Modify: `src/cube_utilities.h`
- Modify: `src/cube_utilities.cpp`
- Test: `test/test_native/test_esp32_utilities.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces:
  - `enum AssignmentParseResult { ASSIGNMENT_OK, ASSIGNMENT_UNASSIGNED, ASSIGNMENT_MISSING, ASSIGNMENT_MALFORMED }`
  - `struct CubeAssignment { uint32_t generation; int slot; }`
  - `AssignmentParseResult parseAssignmentRecord(const char* json, CubeAssignment* out)`
  - `int resolveAssignedSlot(AssignmentParseResult result, int record_slot, bool authority_latched, int compiled_cube_id)` — returns the slot to use, or `-1` for UNASSIGNED.

- [ ] **Step 1: Write the failing tests**

Add to `test/test_native/test_esp32_utilities.cpp` (place after the existing MAC-table tests), then register them in `main()` immediately after the last `RUN_TEST(test_findCubeIpOctet_unknown);` line:

```cpp
    RUN_TEST(test_parseAssignmentRecord_assigned);
    RUN_TEST(test_parseAssignmentRecord_unassigned);
    RUN_TEST(test_parseAssignmentRecord_missing);
    RUN_TEST(test_parseAssignmentRecord_malformed);
    RUN_TEST(test_resolveAssignedSlot);
```

```cpp
void test_parseAssignmentRecord_assigned() {
    CubeAssignment a;
    TEST_ASSERT_EQUAL(ASSIGNMENT_OK,
        parseAssignmentRecord("{\"protocol\": 1, \"generation\": 3, \"slot\": 4}", &a));
    TEST_ASSERT_EQUAL(3, a.generation);
    TEST_ASSERT_EQUAL(4, a.slot);

    // Key order and whitespace must not matter.
    TEST_ASSERT_EQUAL(ASSIGNMENT_OK,
        parseAssignmentRecord("{\"slot\":16,\"generation\":7,\"protocol\":1}", &a));
    TEST_ASSERT_EQUAL(7, a.generation);
    TEST_ASSERT_EQUAL(16, a.slot);
}

void test_parseAssignmentRecord_unassigned() {
    CubeAssignment a;
    TEST_ASSERT_EQUAL(ASSIGNMENT_UNASSIGNED,
        parseAssignmentRecord("{\"protocol\": 1, \"generation\": 5, \"slot\": null}", &a));
    // The generation of an unassigned record still matters to the server.
    TEST_ASSERT_EQUAL(5, a.generation);
    TEST_ASSERT_EQUAL(-1, a.slot);
}

void test_parseAssignmentRecord_missing() {
    CubeAssignment a;
    // No retained record at all, or a cleared (empty payload) one.
    TEST_ASSERT_EQUAL(ASSIGNMENT_MISSING, parseAssignmentRecord(nullptr, &a));
    TEST_ASSERT_EQUAL(ASSIGNMENT_MISSING, parseAssignmentRecord("", &a));
}

void test_parseAssignmentRecord_malformed() {
    CubeAssignment a;
    // Wrong protocol version.
    TEST_ASSERT_EQUAL(ASSIGNMENT_MALFORMED,
        parseAssignmentRecord("{\"protocol\": 2, \"generation\": 1, \"slot\": 4}", &a));
    // Missing protocol.
    TEST_ASSERT_EQUAL(ASSIGNMENT_MALFORMED,
        parseAssignmentRecord("{\"generation\": 1, \"slot\": 4}", &a));
    // Missing generation.
    TEST_ASSERT_EQUAL(ASSIGNMENT_MALFORMED,
        parseAssignmentRecord("{\"protocol\": 1, \"slot\": 4}", &a));
    // Missing slot key entirely (distinct from an explicit null).
    TEST_ASSERT_EQUAL(ASSIGNMENT_MALFORMED,
        parseAssignmentRecord("{\"protocol\": 1, \"generation\": 1}", &a));
    // Slot out of the legal 1..16 range.
    TEST_ASSERT_EQUAL(ASSIGNMENT_MALFORMED,
        parseAssignmentRecord("{\"protocol\": 1, \"generation\": 1, \"slot\": 0}", &a));
    TEST_ASSERT_EQUAL(ASSIGNMENT_MALFORMED,
        parseAssignmentRecord("{\"protocol\": 1, \"generation\": 1, \"slot\": 17}", &a));
    // Not JSON.
    TEST_ASSERT_EQUAL(ASSIGNMENT_MALFORMED, parseAssignmentRecord("garbage", &a));
}

void test_resolveAssignedSlot() {
    // A valid record always wins.
    TEST_ASSERT_EQUAL(4, resolveAssignedSlot(ASSIGNMENT_OK, 4, false, 1));
    TEST_ASSERT_EQUAL(4, resolveAssignedSlot(ASSIGNMENT_OK, 4, true, 1));

    // Explicitly unassigned: idle, never the compiled slot.
    TEST_ASSERT_EQUAL(-1, resolveAssignedSlot(ASSIGNMENT_UNASSIGNED, -1, false, 1));
    TEST_ASSERT_EQUAL(-1, resolveAssignedSlot(ASSIGNMENT_UNASSIGNED, -1, true, 1));

    // Pre-cutover (authority never latched): fall back to the compiled cube_id.
    TEST_ASSERT_EQUAL(1, resolveAssignedSlot(ASSIGNMENT_MISSING, -1, false, 1));
    TEST_ASSERT_EQUAL(1, resolveAssignedSlot(ASSIGNMENT_MALFORMED, -1, false, 1));

    // Once authority is latched: fail closed, no compiled fallback.
    TEST_ASSERT_EQUAL(-1, resolveAssignedSlot(ASSIGNMENT_MISSING, -1, true, 1));
    TEST_ASSERT_EQUAL(-1, resolveAssignedSlot(ASSIGNMENT_MALFORMED, -1, true, 1));

    // An unknown MAC has no compiled slot to fall back to.
    TEST_ASSERT_EQUAL(-1, resolveAssignedSlot(ASSIGNMENT_MISSING, -1, false, -1));
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `~/.platformio/penv/bin/platformio test -e native`
Expected: FAIL — compile error, `parseAssignmentRecord` not declared.

- [ ] **Step 3: Declare the interface**

Add to `src/cube_utilities.h`, after the `findCubeId` declaration:

```cpp
// Slot assignment record published retained on cube/assign/{MAC}.
enum AssignmentParseResult {
  ASSIGNMENT_OK,          // record names a slot
  ASSIGNMENT_UNASSIGNED,  // record says "slot": null
  ASSIGNMENT_MISSING,     // no record, or an empty (cleared) payload
  ASSIGNMENT_MALFORMED,   // wrong protocol, missing field, or unparseable
};

struct CubeAssignment {
  uint32_t generation;
  int slot;  // -1 when unassigned
};

// Parses a cube/assign/{MAC} payload. `out` is always written: on any non-OK
// result slot is -1, and generation is 0 unless the record carried one.
AssignmentParseResult parseAssignmentRecord(const char* json, CubeAssignment* out);

// The slot to run as. Returns -1 for UNASSIGNED: no slot topics, idle display.
// Falls back to compiled_cube_id for a missing/malformed record ONLY while
// authority has never been latched; after that it fails closed.
int resolveAssignedSlot(AssignmentParseResult result, int record_slot,
                        bool authority_latched, int compiled_cube_id);
```

- [ ] **Step 4: Implement it**

Add to `src/cube_utilities.cpp`, after `findCubeId` (add `#include <stdlib.h>` at the top if it is not already there):

```cpp
// Points just past the colon following "key", or nullptr if the key is absent.
static const char* findJsonValue(const char* json, const char* key) {
  char pattern[24];
  snprintf(pattern, sizeof(pattern), "\"%s\"", key);
  const char* found = strstr(json, pattern);
  if (found == nullptr) {
    return nullptr;
  }
  const char* colon = strchr(found + strlen(pattern), ':');
  if (colon == nullptr) {
    return nullptr;
  }
  const char* value = colon + 1;
  while (*value == ' ') {
    ++value;
  }
  return value;
}

// Reads a non-negative integer value. Returns false if it is absent or not a number.
static bool readJsonInt(const char* json, const char* key, long* out) {
  const char* value = findJsonValue(json, key);
  if (value == nullptr || *value < '0' || *value > '9') {
    return false;
  }
  *out = strtol(value, nullptr, 10);
  return true;
}

AssignmentParseResult parseAssignmentRecord(const char* json, CubeAssignment* out) {
  out->generation = 0;
  out->slot = -1;

  if (json == nullptr || json[0] == '\0') {
    return ASSIGNMENT_MISSING;
  }

  long protocol = 0;
  if (!readJsonInt(json, "protocol", &protocol) || protocol != 1) {
    return ASSIGNMENT_MALFORMED;
  }

  long generation = 0;
  if (!readJsonInt(json, "generation", &generation)) {
    return ASSIGNMENT_MALFORMED;
  }
  out->generation = (uint32_t)generation;

  const char* slot_value = findJsonValue(json, "slot");
  if (slot_value == nullptr) {
    return ASSIGNMENT_MALFORMED;
  }
  if (strncmp(slot_value, "null", 4) == 0) {
    return ASSIGNMENT_UNASSIGNED;
  }

  long slot = 0;
  if (!readJsonInt(json, "slot", &slot) || slot < 1 || slot > 16) {
    return ASSIGNMENT_MALFORMED;
  }
  out->slot = (int)slot;
  return ASSIGNMENT_OK;
}

int resolveAssignedSlot(AssignmentParseResult result, int record_slot,
                        bool authority_latched, int compiled_cube_id) {
  switch (result) {
    case ASSIGNMENT_OK:
      return record_slot;
    case ASSIGNMENT_UNASSIGNED:
      return -1;
    default:
      return authority_latched ? -1 : compiled_cube_id;
  }
}
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `~/.platformio/penv/bin/platformio test -e native`
Expected: PASS — the five new tests and every pre-existing test.

- [ ] **Step 6: Commit**

```bash
cd /Users/stephenng/programming/blockwords/cube-pn5180
git add src/cube_utilities.h src/cube_utilities.cpp test/test_native/test_esp32_utilities.cpp
git commit -m "feat: parse and resolve cube slot assignment records"
```

---

### Task B2: Take the slot from the assignment at boot, persisted in NVS

**Repo:** `cube-pn5180`.

**Files:**
- Create: `src/cube_slot_store.h`, `src/cube_slot_store.cpp`
- Modify: `src/main.cpp` (globals ~`257`; `DisplayManager` ~`369`; `getCubeIpOctet` ~`842`; `onConnectionEstablished` ~`1120`; `loop()`)

**Interfaces:**
- Consumes: `parseAssignmentRecord`, `resolveAssignedSlot`, `CubeAssignment` (Task B1); `makeMqttClientId` (step-1 plan).
- Produces:
  - `struct StoredSlot { int slot; uint32_t generation; bool authority_latched; }`
  - `StoredSlot loadStoredSlot()`, `void saveStoredSlot(int slot, uint32_t generation)`, `void latchAuthority()`
  - In `main.cpp`: `static int applied_slot`, `static uint32_t applied_generation`, `static String mac_nocolons`, `static String boot_id`, `void applySlot(int slot)`, `bool slotIsResolved()`.
  - `DisplayManager::setSlotRotation(int slot)`.

- [ ] **Step 1: Create the NVS store**

Create `src/cube_slot_store.h`:

```cpp
#pragma once

#include <Arduino.h>

// The slot assignment this cube last applied, persisted across deep sleep and
// reboot. Namespace "cubepool" is independent of the RTC-memory sleep state.
struct StoredSlot {
  int slot;                 // -1 when unassigned or never stored
  uint32_t generation;
  bool authority_latched;   // once true, never cleared
};

StoredSlot loadStoredSlot();
void saveStoredSlot(int slot, uint32_t generation);
void latchAuthority();
```

Create `src/cube_slot_store.cpp`:

```cpp
#include "cube_slot_store.h"

#include <Preferences.h>

static const char* NVS_NAMESPACE = "cubepool";
static const char* KEY_SLOT = "slot";
static const char* KEY_GENERATION = "gen";
static const char* KEY_AUTHORITY = "auth";

StoredSlot loadStoredSlot() {
  Preferences prefs;
  StoredSlot stored = {-1, 0, false};
  if (prefs.begin(NVS_NAMESPACE, true)) {
    stored.slot = prefs.getInt(KEY_SLOT, -1);
    stored.generation = prefs.getUInt(KEY_GENERATION, 0);
    stored.authority_latched = prefs.getUChar(KEY_AUTHORITY, 0) != 0;
    prefs.end();
  }
  return stored;
}

void saveStoredSlot(int slot, uint32_t generation) {
  Preferences prefs;
  if (prefs.begin(NVS_NAMESPACE, false)) {
    prefs.putInt(KEY_SLOT, slot);
    prefs.putUInt(KEY_GENERATION, generation);
    prefs.end();
  }
}

void latchAuthority() {
  Preferences prefs;
  if (prefs.begin(NVS_NAMESPACE, false)) {
    prefs.putUChar(KEY_AUTHORITY, 1);
    prefs.end();
  }
}
```

- [ ] **Step 2: Add the rotation setter to DisplayManager**

In `src/main.cpp`, the `DisplayManager` constructor sets `rotation = (cube_id_int <= 6) ? 2 : 0;` (~`369`) from a value that is fixed before any assignment can arrive. Add this public method next to the other display setters:

```cpp
  void setSlotRotation(int slot) {
    rotation = (slot <= 6) ? 2 : 0;
    led_display->setRotation(rotation);
    is_dirty = true;
  }
```

- [ ] **Step 3: Add the slot state and split identity from the compiled table**

In `src/main.cpp`, add `#include "cube_slot_store.h"` next to the other project includes, and add these globals beside `cube_identifier` (~`257`):

```cpp
static int compiled_cube_id = -1;      // logical slot from the compiled table
static int applied_slot = -1;          // slot in use this boot; -1 = unassigned
static uint32_t applied_generation = 0;
static bool authority_latched = false;
static bool slot_resolved = false;
static unsigned long assignment_wait_started = 0;
static String mac_nocolons;
static String boot_id;
static String mqtt_topic_assign;

static const unsigned long ASSIGNMENT_WAIT_MS = 3000;
```

In `getCubeIpOctet()` (~`842`), record the compiled slot without committing to it as the identity. Replace `cube_identifier = cube_id;` with:

```cpp
  compiled_cube_id = entry ? entry->cube_id : -1;
```

and leave the rest of the function (RGB order, `configurePins`, the `ip_octet` return added by the step-1 plan) untouched. `configurePins(cube_id)` uses `cube_id` only in log strings, so passing the compiled value is correct.

`cube_identifier` is now set exclusively by `applySlot()`. Its remaining readers — the diagnostic payloads at `src/main.cpp:1393`, `:1422`, `:1449`, `:1465` — must not run before the slot resolves, which `slotIsResolved()` guarantees in Step 5.

In `setup()`, after `setupWiFiConnection()` and before `DisplayManager` is constructed (~`1554`), seed the boot identity:

```cpp
  mac_nocolons = removeColonsFromMac(WiFi.macAddress());
  char boot_id_buf[9];
  snprintf(boot_id_buf, sizeof(boot_id_buf), "%08X", esp_random());
  boot_id = boot_id_buf;

  StoredSlot stored = loadStoredSlot();
  authority_latched = stored.authority_latched;
```

Then change the `DisplayManager` construction from the old `String cube_id = cube_identifier;` to a value that always exists this early:

```cpp
  String cube_id = String(compiled_cube_id > 0 ? compiled_cube_id : 0);
  display_manager = new DisplayManager(cube_id);
```

- [ ] **Step 4: Write `applySlot` and the resolution path**

Add above `onConnectionEstablished()` in `src/main.cpp`:

```cpp
bool slotIsResolved() {
  return slot_resolved && applied_slot > 0;
}

// Installs the logical slot for this boot: topic base, rotation, slot-scoped
// subscriptions, and the initial neighbor publishes. Called at most once per
// boot; a later assignment naming a different slot reboots instead (see
// handleAssignmentRecord), which is the design's assign-then-reboot workflow.
void applySlot(int slot) {
  slot_resolved = true;
  applied_slot = slot;

  if (slot <= 0) {
    cube_identifier = "";
    mqtt_topic_cube = "";  // publishAutoSleepFlag() already treats this as "no slot"
    display_manager->displayDebugMessage("NO SLOT");
    debugSend("unassigned: idle");
    return;
  }

  cube_identifier = String(slot);
  display_manager->setSlotRotation(slot);
  subscribeSlotTopics();
  debugSend((String("slot ") + cube_identifier).c_str());
}
```

Move the body of today's `onConnectionEstablished()` — from `mqtt_topic_cube = ...` (`:1124`) through the `mqtt_client.publish(mqtt_topic_cube_right, "-", true);` block and the `last_right_published` assignment (`:1170`) — verbatim into a new `void subscribeSlotTopics()` defined immediately above `applySlot`. Do **not** move the `debugSend("MQTT connected")` line or the `last_activity_time` initialization at the end; those stay in `onConnectionEstablished`.

- [ ] **Step 5: Rewrite `onConnectionEstablished` and add the record handlers**

Replace `onConnectionEstablished()` (`src/main.cpp:1120`) with:

```cpp
void handleAuthorityMarker(const String& message) {
  if (message.indexOf("\"authoritative\"") < 0 || message.indexOf("true") < 0) {
    return;
  }
  if (!authority_latched) {
    authority_latched = true;
    latchAuthority();  // never cleared: a later absent marker cannot re-enable fallback
    debugSend("authority latched");
  }
}

void handleAssignmentRecord(const String& message) {
  CubeAssignment assignment;
  AssignmentParseResult result = parseAssignmentRecord(message.c_str(), &assignment);
  int slot = resolveAssignedSlot(result, assignment.slot, authority_latched, compiled_cube_id);

  if (!slot_resolved) {
    applied_generation = assignment.generation;
    saveStoredSlot(slot, assignment.generation);
    applySlot(slot);
    return;
  }

  if (slot != applied_slot) {
    // Persist first so the reboot converges in one pass instead of looping.
    saveStoredSlot(slot, assignment.generation);
    debugSend("slot changed: rebooting");
    delay(200);
    ESP.restart();
  }
  applied_generation = assignment.generation;
}

void onConnectionEstablished() {
  debugSend("MQTT connected");

  mqtt_topic_assign = String("cube/assign/") + mac_nocolons;

  mqtt_client.subscribe("cube/roster/authoritative", handleAuthorityMarker);
  mqtt_client.subscribe(mqtt_topic_assign, handleAssignmentRecord);

  // Slot-independent commands: safe before the slot resolves.
  auto resetActivityTimer = []() { last_activity_time = millis(); };
  mqtt_client.subscribe(String(MQTT_TOPIC_PREFIX_CUBE) + "brightness", [resetActivityTimer](const String& msg) { resetActivityTimer(); display_manager->handleBrightnessCommand(msg); });
  mqtt_client.subscribe(String(MQTT_TOPIC_PREFIX_CUBE) + "reboot", [resetActivityTimer](const String& msg) { resetActivityTimer(); handleRebootCommand(msg); });
  mqtt_client.subscribe(String(MQTT_TOPIC_PREFIX_CUBE) + "sleep_now", handleSleepNowCommand);

  if (slotIsResolved()) {
    // Reconnect after a WiFi/broker bounce: keep playing on the slot already in
    // use. The retained record still arrives and is revalidated above.
    subscribeSlotTopics();
  } else if (!slot_resolved) {
    // Cold boot: always wait for the retained record. Applying the NVS slot
    // first would make a reassignment cost two reboots and would republish
    // retained neighbor state under the old slot — which the replacement cube
    // now holds. The NVS slot is the timeout fallback instead (see loop()).
    assignment_wait_started = millis();
  }

  if (last_activity_time == 0) {
    last_activity_time = millis();
  }
}
```

Move the three slot-independent subscriptions above (`brightness`, `reboot`, `sleep_now`) out of `subscribeSlotTopics()` so they are not registered twice.

- [ ] **Step 6: Add the resolution timeout to `loop()`**

In `loop()`, immediately after the existing `mqtt_client.loop();` call, add:

```cpp
  // No retained assignment arrived within the window: resolve without one.
  // Pre-cutover this is the compiled cube_id; once authority is latched it is
  // UNASSIGNED, never the compiled slot.
  if (!slot_resolved && assignment_wait_started != 0 &&
      millis() - assignment_wait_started >= ASSIGNMENT_WAIT_MS) {
    assignment_wait_started = 0;
    StoredSlot stored = loadStoredSlot();
    // The last applied slot outranks the compiled one as a fallback: it is what
    // this cube was actually assigned, and it survives a broker with lost
    // retained state.
    int fallback = stored.slot > 0 ? stored.slot : compiled_cube_id;
    int slot = resolveAssignedSlot(ASSIGNMENT_MISSING, -1, authority_latched, fallback);
    applied_generation = stored.generation;
    saveStoredSlot(slot, stored.generation);
    applySlot(slot);
  }
```

Then guard every slot-scoped periodic publish in `loop()` — the diagnostic/timing/temperature blocks that read `cube_identifier` (`src/main.cpp:1393`, `:1422`, `:1449`, `:1465`) and the NFC/hall neighbor publish blocks — behind `slotIsResolved()`. The simplest correct edit is to wrap each of those blocks' entry condition with `slotIsResolved() && ...`.

- [ ] **Step 7: Run native tests**

Run: `~/.platformio/penv/bin/platformio test -e native`
Expected: PASS (no native code changed; confirms no regression).

- [ ] **Step 8: Compile for hardware**

Run: `~/.platformio/penv/bin/platformio run -e v1 -e v6 -e v6_with_hall -e v6_with_hall_analog -e v6_with_hall_neighbor`
Expected: SUCCESS on all five; memory within limits.

- [ ] **Step 9: Bench-verify slot resolution (hardware)**

Flash one cube (choose the environment from `config/cube_board_versions.txt`, per the flashing skill) and, with the Phase A server running:

1. Normal boot — serial/UDP debug shows `slot N` matching the cube's roster slot, the display is rotated as before, and it plays normally.
2. Republish that cube's record with a different slot and reboot it:
   `mosquitto_pub -h 192.168.8.247 -t 'cube/assign/<MAC>' -r -m '{"protocol":1,"generation":2,"slot":4}'`
   Expected: after the reboot it reports `slot 4` and answers on `cube/4/...`.
3. Publish `{"protocol":1,"generation":3,"slot":null}` and reboot.
   Expected: display shows `NO SLOT`, and `mosquitto_sub -h 192.168.8.247 -t 'cube/#' -v` shows no slot-scoped traffic from it.
4. Clear the record (`-m '' -r`) and reboot.
   Expected: with no authority marker ever published, it falls back to its compiled `cube_id` — today's behavior.

Restore the cube's correct record afterward.

- [ ] **Step 10: Commit**

```bash
cd /Users/stephenng/programming/blockwords/cube-pn5180
git add src/cube_slot_store.h src/cube_slot_store.cpp src/main.cpp
git commit -m "feat: take logical slot from the retained assignment record"
```

---

### Task B3: MAC-scoped presence, offline last will, and a clean sleep disconnect

**Repo:** `cube-pn5180`.

**Files:**
- Modify: `lib/EspMQTTClient/src/EspMQTTClient.h` (add `disconnect()`)
- Modify: `src/main.cpp` (`setup()`; `enterSleepMode` ~`958`; `applySlot`)

**Interfaces:**
- Consumes: `mac_nocolons`, `boot_id`, `applied_slot`, `applied_generation` (Task B2).
- Produces: `void publishPresence(const char* state)`; retained topic `cube/device/{MAC}/presence`.

- [ ] **Step 1: Give the vendored client a disconnect**

`enterSleepMode()` currently calls `esp_deep_sleep_start()` with no MQTT disconnect, so the broker treats deep sleep as an ungraceful drop and fires the last will — which would overwrite a `sleeping` presence record with `offline`. The vendored `EspMQTTClient` exposes no disconnect, so add one. In `lib/EspMQTTClient/src/EspMQTTClient.h`, in the public section next to `isConnected()`:

```cpp
  // Send a clean MQTT DISCONNECT so the broker does not publish the last will.
  inline void disconnect() { _mqttClient.disconnect(); _mqttConnected = false; };
```

- [ ] **Step 2: Publish presence and install the last will**

In `src/main.cpp`, add above `applySlot`:

```cpp
static String mqtt_topic_presence;

void publishPresence(const char* state) {
  if (mqtt_topic_presence.isEmpty()) {
    return;
  }
  char payload[160];
  snprintf(payload, sizeof(payload),
           "{\"protocol\":1,\"state\":\"%s\",\"boot_id\":\"%s\","
           "\"applied_slot\":%d,\"applied_generation\":%lu}",
           state, boot_id.c_str(), applied_slot, (unsigned long)applied_generation);
  mqtt_client.publish(mqtt_topic_presence, payload, true);
}
```

In `setup()`, immediately after `mac_nocolons` and `boot_id` are set (Task B2 Step 3) and **before the first `mqtt_client.loop()`** — `enableLastWillMessage` is documented as setup-only in `lib/EspMQTTClient/src/EspMQTTClient.h:164`:

```cpp
  mqtt_topic_presence = String("cube/device/") + mac_nocolons + "/presence";
  static String last_will_payload = String("{\"protocol\":1,\"state\":\"offline\",\"boot_id\":\"") + boot_id + "\"}";
  mqtt_client.enableLastWillMessage(mqtt_topic_presence.c_str(), last_will_payload.c_str(), true);
```

`last_will_payload` must be `static`: the library stores the pointer, not a copy.

At the end of `applySlot()` — both the unassigned and the assigned branch — publish the resolved state:

```cpp
  publishPresence("online");
```

- [ ] **Step 3: Announce sleep and disconnect cleanly**

In `enterSleepMode()` (`src/main.cpp:958`), immediately after the `display_manager->displayDebugMessage("sleep...")` / `delay(2000)` lines and **before** the `#ifdef BOARD_V6` display shutdown:

```cpp
  // Only the full client has a last will to suppress. On the maintenance path
  // (handleWakeUp -> enterSleepMode, src/main.cpp:1054) mqtt_client never
  // connected, and calling its loop() there would start a connection attempt
  // milliseconds before deep sleep.
  if (mqtt_client.isConnected()) {
    publishPresence("sleeping");
    mqtt_client.loop();   // flush the presence publish
    delay(100);
    mqtt_client.disconnect();  // clean DISCONNECT: the last will must not overwrite "sleeping"
  }
```

- [ ] **Step 4: Compile for hardware**

Run: `~/.platformio/penv/bin/platformio run -e v1 -e v6 -e v6_with_hall -e v6_with_hall_analog -e v6_with_hall_neighbor`
Expected: SUCCESS on all five.

- [ ] **Step 5: Bench-verify presence transitions (hardware)**

With `mosquitto_sub -h 192.168.8.247 -t 'cube/device/+/presence' -v` running:

1. Boot the cube → one retained `"state":"online"` record with a fresh `boot_id` and the applied slot.
2. `mosquitto_pub -h 192.168.8.247 -t cube/sleep_now -m 1` → a `"state":"sleeping"` record, and **no** `"offline"` record after it. Re-subscribe with `-W 3` to confirm the retained value is still `sleeping`.
3. Yank power while awake → the retained record becomes `"state":"offline"` (last will fired).

Step 2 failing (an `offline` record arriving after `sleeping`) means the disconnect is not reaching the broker before deep sleep — increase the `delay(100)`, do not remove the disconnect.

- [ ] **Step 6: Commit**

```bash
cd /Users/stephenng/programming/blockwords/cube-pn5180
git add lib/EspMQTTClient/src/EspMQTTClient.h src/main.cpp
git commit -m "feat: MAC-scoped presence with offline will and clean sleep disconnect"
```

---

### Task B4: Answer MAC-scoped liveness challenges

**Repo:** `cube-pn5180`.

**Files:**
- Modify: `src/main.cpp` (`onConnectionEstablished`)

**Interfaces:**
- Consumes: `mac_nocolons`, `boot_id`, `applied_slot`, `applied_generation`, `slotIsResolved()` (Task B2).
- Produces: subscription on `cube/device/{MAC}/liveness-request`, non-retained reply on `cube/device/{MAC}/liveness-response`.

The step-4 gate is what enforces nonce and timeout bounds; the responder here just echoes.

- [ ] **Step 1: Add the responder**

In `src/main.cpp`, above `onConnectionEstablished()`:

```cpp
static String mqtt_topic_liveness_response;

// Proof of life for the server's game-start gate: retained presence is not
// accepted as liveness, so the cube must answer a fresh nonce while awake.
void handleLivenessRequest(const String& nonce) {
  if (nonce.isEmpty() || !slotIsResolved()) {
    return;
  }
  char payload[224];
  snprintf(payload, sizeof(payload),
           "{\"protocol\":1,\"nonce\":\"%s\",\"boot_id\":\"%s\","
           "\"generation\":%lu,\"applied_slot\":%d}",
           nonce.c_str(), boot_id.c_str(),
           (unsigned long)applied_generation, applied_slot);
  mqtt_client.publish(mqtt_topic_liveness_response, payload, false);
}
```

- [ ] **Step 2: Subscribe on connect**

In `onConnectionEstablished()`, next to the assignment subscription:

```cpp
  mqtt_topic_liveness_response = String("cube/device/") + mac_nocolons + "/liveness-response";
  mqtt_client.subscribe(String("cube/device/") + mac_nocolons + "/liveness-request", handleLivenessRequest);
```

- [ ] **Step 3: Compile for hardware**

Run: `~/.platformio/penv/bin/platformio run -e v1 -e v6 -e v6_with_hall -e v6_with_hall_analog -e v6_with_hall_neighbor`
Expected: SUCCESS on all five.

- [ ] **Step 4: Bench-verify the challenge/response (hardware)**

```bash
mosquitto_rr -h 192.168.8.247 \
  -t "cube/device/<MAC>/liveness-response" \
  -e "cube/device/<MAC>/liveness-request" -m "nonce-abc123" -W 5
```

Expected: a single JSON reply echoing `"nonce":"nonce-abc123"` with the cube's `boot_id`, generation, and applied slot. `mosquitto_rr` is used (not pub-then-sub) so the subscription is established before the request — the same reason `tools/flash_cubes.sh` uses it for its online check.

Then put the cube to sleep (`mosquitto_pub -h 192.168.8.247 -t cube/sleep_now -m 1`) and repeat: expected **no reply** (the command times out), which is what keeps a sleeping cube from satisfying the step-4 gate.

- [ ] **Step 5: Commit**

```bash
cd /Users/stephenng/programming/blockwords/cube-pn5180
git add src/main.cpp
git commit -m "feat: answer MAC-scoped liveness challenges while awake"
```

---

### Task B5: MAC-scope the maintenance-wake path and revalidate its slot

**Repo:** `cube-pn5180`.

**Files:**
- Modify: `src/main.cpp` (`handleWakeUp` ~`1004-1075`, `publishAutoSleepFlag` ~`950`)

**Interfaces:**
- Consumes: `makeMqttClientId` (step-1 plan); `loadStoredSlot()` (Task B2); `mac_nocolons`, `boot_id`.
- Produces: no new symbols. The keepalive client's ID and its `status` topic become MAC-scoped; `auto_sleep` becomes dual-scoped.

Today the timer-wake path derives its client ID and both topics from the compiled slot (`src/main.cpp:1018-1019`, `:1033`), so a reassigned spare would revert to its old logical identity on every maintenance wake. It runs before `onConnectionEstablished`, so it cannot use the live assignment — it uses the NVS slot instead, and only for the legacy slot-scoped `auto_sleep` topic that `tools/wake.sh` still writes.

- [ ] **Step 1: MAC-scope the identity and dual-scope `auto_sleep`**

In `handleWakeUp()`, replace the client ID and topic construction (`src/main.cpp:1017-1019`):

```cpp
    volatile bool stay_asleep = false;  // Default: wake up unless we see "1"
    StoredSlot stored = loadStoredSlot();
    String client_id = makeMqttClientId(WiFi.macAddress(), "-ka");
    String device_auto_sleep_topic = "cube/device/" + mac_nocolons + "/auto_sleep";
    // tools/wake.sh still clears the slot-scoped flag; honor both until the
    // console owns the MAC map (rollout step 4).
    String slot_auto_sleep_topic =
        stored.slot > 0 ? "cube/" + String(stored.slot) + "/auto_sleep" : String("");
```

Replace the status publish (`:1033`) and the subscribe (`:1037`):

```cpp
      String status_topic = "cube/device/" + mac_nocolons + "/status";
      keepalive_mqtt.publish(status_topic.c_str(), "keep-alive");

      keepalive_mqtt.subscribe(device_auto_sleep_topic.c_str());
      if (!slot_auto_sleep_topic.isEmpty()) {
        keepalive_mqtt.subscribe(slot_auto_sleep_topic.c_str());
      }
```

And in the wake branch that clears the retained flag (`:1058`), clear both:

```cpp
        keepalive_mqtt.publish(device_auto_sleep_topic.c_str(), "", true);
        if (!slot_auto_sleep_topic.isEmpty()) {
          keepalive_mqtt.publish(slot_auto_sleep_topic.c_str(), "", true);
        }
        delay(100);
```

`mac_nocolons` is set in `setup()` before `handleWakeUp()` is called (`src/main.cpp:1564`) — verify that ordering still holds after Task B2's edits, and move the assignment earlier if it does not.

- [ ] **Step 2: Dual-scope the auto-sleep flag on the way down**

Replace `publishAutoSleepFlag()` (`src/main.cpp:950`):

```cpp
void publishAutoSleepFlag() {
  mqtt_client.publish("cube/device/" + mac_nocolons + "/auto_sleep", "1", true);
  if (!mqtt_topic_cube.isEmpty()) {
    mqtt_client.publish(mqtt_topic_cube + "/auto_sleep", "1", true);
  }
  delay(100);  // Give MQTT time to flush before sleep
}
```

The MAC-scoped flag is published even when unassigned, so a cube with no slot still resumes its sleep cycle correctly.

- [ ] **Step 3: Compile for hardware**

Run: `~/.platformio/penv/bin/platformio run -e v1 -e v6 -e v6_with_hall -e v6_with_hall_analog -e v6_with_hall_neighbor`
Expected: SUCCESS on all five.

- [ ] **Step 4: Bench-verify the sleep/wake cycle end to end (hardware)**

1. `mosquitto_pub -h 192.168.8.247 -t cube/sleep_now -m 1` → with `mosquitto_sub -h 192.168.8.247 -t 'cube/#' -v` running, confirm the auto-sleep flag is set on **both** `cube/device/<MAC>/auto_sleep` and `cube/<slot>/auto_sleep`, and the cube sleeps.
2. Wait for a timer wake → confirm `cube/device/<MAC>/status` reports `keep-alive`, that no `cube/<slot>/status` message appears, and that the cube goes back to sleep. The retained presence record stays `sleeping` across this: only the full client publishes presence, and the maintenance path leaves the existing retained value alone.
3. `tools/wake.sh <slot>` (unchanged, still slot-scoped) → the cube wakes fully on its next timer wake. This is the check that the dual-scoping actually preserves today's tooling.
4. Reassign the cube to a different slot (Task B2 Step 9 procedure), reboot, let it sleep, and confirm the next maintenance wake publishes nothing under the **old** slot.

- [ ] **Step 5: Commit**

```bash
cd /Users/stephenng/programming/blockwords/cube-pn5180
git add src/main.cpp
git commit -m "feat: MAC-scope the maintenance wake path; dual-scope auto_sleep"
```

---

## Full-system acceptance (run after both phases)

Not a commit-gated task — run this before declaring the two rollout steps done.

- [ ] Start the server with the seeded roster and boot all 12 cubes. Every cube reports its today-slot, and a 6-cube and a 12-cube game play identically to before.
- [ ] Confirm 18 retained `cube/assign/#` records and 12+ retained `cube/device/+/presence` records with `state: online`.
- [ ] Swap in software only: `assign_slot` a backup MAC to a primary's slot via a Python one-liner against `src/data/cube_roster.json`, restart the server, and reboot the two affected cubes. The backup takes the slot; the primary reports `NO SLOT`. Restore afterward.
- [ ] Kill the server between the roster save and the publish, restart it, and confirm both affected records converge with no double-claimed slot.
- [ ] Leave the cubes idle past the sleep threshold and confirm no assignment changes and the sleep/wake cycle is unchanged.

## What is deliberately NOT in this plan

Rollout steps 4 and 5 of `2026-07-25-dynamic-cube-pool.md`:

- Publishing `cube/roster/authoritative` (the authority cutover). Firmware latches and fails closed correctly once it appears; nothing publishes it yet.
- The console `assign-slot` / `swap` action with hardware-class checks, and moving the roster to a `pi-deploy`-managed path.
- The MAC-verified, liveness-gated game-start gate (the server side of Task B4's responder).
- Retiring the slot-scoped `auto_sleep` / `status` topics and making `tools/check_cubes.py`, `tools/update_cubes.py`, and `tools/update.sh` inventory-aware — plus the `.41`–`.46` DHCP reservation, both listed as deferred prerequisites in the step-1 plan.
- Server-side NFC tag resolution with sender-MAC/generation provenance, and retiring `cube/right/{id}` and the compiled `lookupCubeNumberByTag`.
