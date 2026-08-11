# Cold-boot splash before the wake check-in

## Problem

`setup()` runs `handleWakeUp()` before it builds the `DisplayManager`. A cold boot
that finds a retained `auto_sleep` flag therefore re-enters deep sleep having
painted nothing, and the panel stays dark from power-on to power-off.

That is correct sleep behaviour — `runWakeCheckIn()` deliberately re-reads the
flag on a reset so a stored cube that brownouts or watchdogs goes back to sleep
rather than burning battery — but it is indistinguishable from dead hardware.
Observed 2026-08-11: a chip with a stale retained `cube/1/auto_sleep=1` was
diagnosed as bad firmware, and swapping in another ESP32 appeared to clear it
only because that MAC read a different flag.

## Design

Build the `DisplayManager` and paint `GIT_TIMESTAMP` before `handleWakeUp()`,
on a power-on only:

```cpp
String cube_id = String(compiled_cube_id);

if (is_first_boot && esp_reset_reason() == ESP_RST_POWERON) {
  display_manager = new DisplayManager(cube_id);
  display_manager->clearDebugDisplay();
  display_manager->displayDebugMessage(GIT_TIMESTAMP);
}

handleWakeUp();
```

The existing init after `handleWakeUp()` becomes conditional on
`display_manager == nullptr`, so a timer or button wake is unchanged.

A cold boot now shows the build timestamp, and — if the flag sends it back to
sleep — `enterSleepMode()`'s existing `display_manager != nullptr` branch paints
`"sleep..."`, dwells 2 s and tears DMA down before dropping the rail. The panel
tells you which of the two happened.

### Why these boundaries

**Power-on only.** A timer wake is a keep-alive check-in that holds the TPS22975
off; powering the panel for it would cost the current saving the pulse exists to
make. A button wake reaches `stayAwake()` without consulting the flags, so it was
never dark.

`is_first_boot` alone is too wide: `esp_sleep_get_wakeup_cause()` returns
`ESP_SLEEP_WAKEUP_UNDEFINED` for brownout, watchdog, panic and software resets as
well as power-on, so gating on it would light the panel and dwell 2 s on
`"sleep..."` for every glitch a stored cube suffers — the battery burn
`runWakeCheckIn()` re-reads the flag to avoid. `esp_reset_reason()` separates
them. An EN-pin reset reports `ESP_RST_POWERON` (confirmed from this chip's boot
ROM banner, `rst:0x1 (POWERON_RESET)`, on a DTR/RTS reset), so a bench reset
still exercises the path.

**After `setupWiFiConnection()`.** The 5 V rail is raised early on a first boot
and WiFi setup already gives it time to settle, so no `POWER_RAIL_SETTLE_MS`
delay is needed and I2S DMA starts exactly where it starts today. Painting
earlier would be ~2 s more responsive but would start DMA during WiFi init,
which nothing in this firmware currently does.

**Sleep policy untouched.** `resolveWakeAction()` and `runWakeCheckIn()` are not
modified. The alternative — treating `ESP_RST_POWERON` as intentional use, like a
button press — was considered and rejected: a jostled battery contact reads as a
power-on, which is the case the current design explicitly sends back to sleep.

## Verification

Not unit-testable: this is ordering inside `setup()`, and `DisplayManager` needs
the panel. Bench procedure on a cube with `cube/N/auto_sleep` retained `1`:

0. First confirm the panel lights at all — `tools/wake.sh N`, power-cycle, see
   output. Nothing below distinguishes "splash missing" from "panel broken".
1. Re-set the flag (`tools/sleep.sh`), power-cycle. Panel shows the build
   timestamp, then `sleep...`, then dark. Serial shows
   `Entering deep sleep mode...`.
2. `tools/wake.sh N`, power-cycle. Panel shows the timestamp and stays up;
   serial shows `Waking fully - continuing setup`.
3. Let it auto-sleep, then confirm a timer wake still paints nothing.

Flash with an explicit env. `config/cube_board_versions.txt` on `main` still maps
`80:F3:DA:54:53:B8` to the retired `v6_with_hall_neighbor`, so
`tools/flash_cubes.sh` would select an env this change was never compiled
against.
