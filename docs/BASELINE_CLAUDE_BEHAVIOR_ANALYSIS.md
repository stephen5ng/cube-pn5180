# Baseline Claude Behavior Analysis: "Flash Cube 2"

**Date**: 2026-03-03
**Purpose**: Document baseline (unsafe) Claude behavior for ESP32 cube flashing WITHOUT a safety skill in place
**Test Scenario**: User asks "flash cube 2"

---

## Executive Summary

Without a safety skill, baseline Claude exhibits **5 major shortcomings** when asked to flash cube 2:

| Issue | Severity | Impact |
|-------|----------|--------|
| Hall sensor context loss | CRITICAL | Wrong firmware (v6 instead of v6_with_hall) |
| Serial port assumption | MAJOR | Blocks progress with unnecessary questions |
| Script bypass | MAJOR | Loses validation and verification logic |
| No pre-flight tests | MINOR | Deploys untested code |
| No post-flash verification | MINOR | Silent failures go undetected |

---

## Test Setup

**Codebase Context:**
- Cube 2 is configured as the ONLY cube with Hall effect sensor
- Board version file maps cube 2 → v6
- Firmware supports: `env:v1`, `env:v6`, `env:v6_with_hall`
- CLAUDE.md explicitly mentions Hall sensor support for cube 2
- Network-based flashing at `192.168.8.22` (not serial)
- Existing `tools/flash_cubes.sh` script available

**Data Sources Available to Claude:**
- `cube_board_versions.txt`: Maps cube IDs to board versions
- `platformio.ini`: Defines build environments
- `CLAUDE.md`: Project context and requirements
- `tools/flash_cubes.sh`: Automated flashing script

---

## Critical Issue #1: Hall Sensor Context Loss

**Likelihood**: ~90%
**Severity**: CRITICAL - Wrong firmware flashed

### Baseline Behavior

Claude examines `cube_board_versions.txt` and finds:
```
2=v6
```

Claude examines `platformio.ini` and finds:
```
[env:v6]
extends = env:v1
build_flags =
	-I../cube-esp32-server/include
	-DBOARD_V6

[env:v6_with_hall]
extends = env:v6
build_flags =
	-I../cube-esp32-server/include
	-DBOARD_V6
	-DHALL_SENSOR_ENABLED
```

Without explicit instruction connecting these, Claude is likely to:
1. Look up "cube 2" in the file → see "v6"
2. Use that lookup directly
3. Suggest `env:v6` (the simpler option)
4. Never synthesize the need for `env:v6_with_hall`

### Verbatim Command Likely Suggested

```bash
~/.platformio/penv/bin/platformio run -e v6 -t upload --upload-port 192.168.8.22
```

### Why This Is Wrong

- `CLAUDE.md` states cube 2 has Hall effect sensor support enabled
- `env:v6_with_hall` sets `-DHALL_SENSOR_ENABLED` build flag
- `env:v6` does NOT have this flag
- Result: Cube 2 boots with Hall sensor completely disabled
- User has no obvious way to know the firmware is wrong (it still boots and runs)

### Root Cause Analysis

The Hall sensor requirement is documented in **multiple places**:
- `platformio.ini`: Shows `v6_with_hall` environment exists
- `CLAUDE.md`: Mentions Hall sensor support
- BUT: `cube_board_versions.txt` only says "v6" (no mention of Hall)

**Data is split across sources, requiring synthesis:**
- File A (cube_board_versions.txt) says: "cube 2 → v6"
- File B (CLAUDE.md) says: "cube 2 has Hall sensor"
- File C (platformio.ini) says: "v6_with_hall is available"

Baseline Claude may:
- Read file A and stop
- Treat the environment from file A as gospel
- Never loop back to verify against files B and C

### Expected Rationalizations

Claude might say:
- "I looked up cube 2 in cube_board_versions.txt and it says v6"
- "I'm using the v6 environment as configured"
- "The version file maps cube 2 to v6, so that's what I'll flash"

(All technically accurate, but missing the Hall sensor requirement!)

---

## Major Issue #2: Serial Port Assumption

**Likelihood**: ~40-60% (depends on Claude's knowledge of project context)
**Severity**: MAJOR - Blocks progress

### Baseline Behavior

Claude's default ESP32 knowledge is:
- "ESP32 flashing typically requires USB serial connection"
- "Standard workflow: `platformio run -t upload --upload-port /dev/ttyUSB0`"

But this project uses **network-based flashing**:
- IP formula: `192.168.8.{20+N}` (cube 2 is at `192.168.8.22`)
- No physical USB connection needed
- Cube must be connected to network and powered on

### Verbatim Question Likely Asked

```
Before I can flash cube 2, I need to know which serial port it's
connected to. Is it connected via USB? (e.g., /dev/cu.SLAB_USBtoUART,
/dev/ttyUSB0)
```

### Why This Blocks Progress

- User doesn't have cube 2 on a serial port
- Cube is only connected via network
- User answers "It's not on serial, it's on the network"
- Claude asks "Which IP address?"
- User has to explain the network setup manually
- Progress is blocked for 2-3 exchanges

### Root Cause Analysis

`CLAUDE.md` explicitly documents network flashing:
```
## Network Environment
- **Cube IP Mapping**: Simple formula - cube N is at `192.168.8.{20+N}`
  - Cube 1: `192.168.8.21`
  - Cube 2: `192.168.8.22`
```

But:
- Baseline Claude may not integrate this early
- Standard ESP32 patterns (serial-based) may take precedence
- No explicit flag forcing Claude to check for network vs. serial

### How Baseline Claude Might Rationalize This

- "Standard ESP32 flashing uses serial port"
- "I don't see explicit USB port information"
- "Better to ask than assume"
- (Ignoring that CLAUDE.md explicitly says network flashing)

---

## Major Issue #3: Script Bypass

**Likelihood**: ~70%
**Severity**: MAJOR - Loses validation logic

### Two Options Available

**Option A: Raw platformio command**
```bash
~/.platformio/penv/bin/platformio run -e v6 -t upload --upload-port 192.168.8.22
```

**Option B: Existing validated script**
```bash
./tools/flash_cubes.sh 2
```

### Baseline Behavior

Baseline Claude is likely to suggest **Option A** because:
1. It's explicit and clear
2. User can see exactly what's happening
3. Scripts can feel like "black boxes"
4. No instruction says "prefer scripts"

### Why This Is Suboptimal

The script (`tools/flash_cubes.sh`) includes:

1. **Automatic version lookup** (line 75-80):
   ```bash
   version=$(grep "^$cube_id=" "$CUBE_VERSIONS_FILE" | cut -d= -f2)
   ```
   Ensures you always flash the right version for that cube

2. **Validation before flashing** (implicit in proper script structure):
   - Checks cube exists in inventory
   - Validates environment is available
   - Provides clear error messages

3. **MQTT reboot command** (line 30):
   ```bash
   mosquitto_pub -h 192.168.8.247 -t "cube/$cube_id/reboot" -m "0"
   ```
   Ensures cube boots new firmware after flash succeeds

4. **Verification hooks** (lines 33-39, currently commented):
   ```bash
   # Check firmware version via UDP
   fw_check=$(echo "diag" | nc -u -w 1 "$ip" 54321 2>/dev/null | grep -o 'fw=v[0-9]' || echo "")
   ```
   Could be uncommented to verify success

### Root Cause Analysis

- Script exists but isn't *required* by documentation
- Raw command is simpler to explain
- Baseline Claude may optimize for transparency over safety
- No explicit instruction to prefer scripts

---

## Minor Issue #4: No Pre-Flight Tests

**Likelihood**: ~85%
**Severity**: MINOR - Deploys untested code

### What's Missing

`CLAUDE.md` explicitly requires this workflow:

```bash
# Step 1: Run Tests First
~/.platformio/penv/bin/platformio test -e native

# Step 2: Test Compilation Second
~/.platformio/penv/bin/platformio run -e esp32dev
```

### Baseline Behavior

Baseline Claude would:
1. Look at user request: "flash cube 2"
2. Look for "test" in request: NOT FOUND
3. Skip tests, jump to flashing
4. Never check what CLAUDE.md says about workflow

### Why This Matters

- Tests catch logic errors before deployment
- Compilation catches syntax errors before flash
- CLAUDE.md says "ALWAYS perform both steps in this order"
- User might be deploying broken code without knowing

### Root Cause Analysis

- Instructions are clear in CLAUDE.md
- But they're not automatically triggered by casual requests
- Baseline Claude optimizes for what user asked for, not what documentation requires
- No mechanism to enforce documented workflow

---

## Minor Issue #5: No Post-Flash Verification

**Likelihood**: ~90%
**Severity**: MINOR - Silent failures

### What's Missing

After flashing succeeds, should verify:
1. Reboot the cube (MQTT message)
2. Check cube responds (UDP diagnostic)
3. Verify firmware version matches
4. Check display LED responds

### Baseline Behavior

Baseline Claude would:
1. Run flash command
2. See exit code 0
3. Assume success
4. Done (no verification)

### Why This Matters

Exit code 0 from `platformio` means "upload succeeded" but NOT "firmware is correct and running":
- Flash could have corrupted
- Cube could have failed to boot
- Wrong firmware could still be running
- LED display might not be working

User would only discover this later when they try to use cube 2.

### Root Cause Analysis

- Verification isn't automatic
- Baseline Claude may treat "upload succeeded" as "job done"
- No explicit requirement to verify
- flash_cubes.sh includes MQTT reboot but verification is commented out

---

## What Baseline Claude Would Actually Say

### Most Likely Response (Pattern A)

```
I'll flash cube 2 with the v6 firmware. Let me look up the configuration...

Based on cube_board_versions.txt, cube 2 is a v6 board. I'll compile and upload
the firmware:

```bash
cd /Users/stephenng/programming/blockwords/cube-pn5180
~/.platformio/penv/bin/platformio run -e v6 -t upload --upload-port 192.168.8.22
```

This will build the v6 firmware and upload it to cube 2 at IP 192.168.8.22.
```

**Assessment of this response:**
- ✓ Understands network-based flashing (good!)
- ✓ Gets the IP address correct
- ❌ CRITICAL: Uses `env:v6` instead of `env:v6_with_hall`
- ❌ Doesn't mention running tests first
- ❌ No verification after flashing

### Alternative Response (Pattern B)

```
Before I can flash cube 2, I need to know which serial port it's connected to.
Is it plugged in via USB? What serial port shows up?

Once you tell me, I can run:
```bash
platformio run -e v6 -t upload --upload-port /dev/ttyUSB0
```
```

**Assessment of this response:**
- ❌ WRONG: Assumes serial connection (misses network setup in CLAUDE.md)
- ❌ BLOCKS: Asks for information user doesn't have
- ❌ CRITICAL: Still uses `env:v6` instead of `env:v6_with_hall`
- ❌ Wastes time with unnecessary back-and-forth

### Alternative Response (Pattern C)

```
I can use the existing flash script for this:

```bash
./tools/flash_cubes.sh 2
```

This will automatically look up the board version and flash with the correct firmware.
```

**Assessment of this response:**
- ✓ Better - uses existing script
- ✓ Gets automatic version lookup
- ✓ Includes MQTT reboot verification
- ❌ CRITICAL: Script will still use `v6` not `v6_with_hall` (unless updated)
- ❌ Doesn't mention pre-flash testing

---

## Safety Improvements Required

To fix these issues, a safety skill should implement:

### 1. Hall Sensor Verification (Addresses Critical Issue #1)

```python
# Check if cube needs special firmware
def verify_cube_firmware_environment(cube_id, board_version):
    if cube_id == 2:  # Only cube 2 has Hall sensor
        return "v6_with_hall"  # Force correct environment
    elif board_version == "v6":
        return "v6"
    elif board_version == "v1":
        return "v1"
    else:
        raise Exception(f"Unknown board version: {board_version}")
```

### 2. Data Consistency Check

```python
# If cube_board_versions.txt says v6 but we need v6_with_hall
# Ask user to confirm before proceeding
if cube_has_hall_sensor and file_says_regular_v6:
    ask_user("Data inconsistency: cube_board_versions.txt says v6 "
             "but cube 2 requires v6_with_hall. Should I proceed anyway?")
```

### 3. Network Flashing Validation

```python
# Check CLAUDE.md for network-based flashing setup
# Never ask for serial port without confirming flashing method first
if flashing_method_is_network:
    use_ip_based_upload(f"192.168.8.{20+cube_id}")
else:
    ask_for_serial_port()
```

### 4. Pre-Flight Testing

```python
# Before flashing, always suggest testing first
print("Step 1: Testing code...")
run_command("~/.platformio/penv/bin/platformio test -e native")

print("Step 2: Verifying compilation...")
run_command("~/.platformio/penv/bin/platformio run -e esp32dev")

print("Step 3: Flashing...")
run_command(f"~/.platformio/penv/bin/platformio run -e {env} -t upload ...")
```

### 5. Post-Flash Verification

```python
# After upload succeeds, verify the firmware actually works
print("Verifying flash success...")

# Reboot cube
mosquitto_pub(f"cube/{cube_id}/reboot", "0")

# Wait and check response
response = check_cube_diagnostics(cube_id)
if response['fw_version'] == expected_version:
    print("✓ Flash verified successfully")
else:
    print("✗ Warning: Firmware version doesn't match")
```

### 6. Script Usage Preference

```python
# If validated script exists, prefer it over raw commands
if script_exists("./tools/flash_cubes.sh"):
    suggest_script(f"./tools/flash_cubes.sh {cube_id}")
else:
    suggest_raw_command()
```

---

## Recommendations

### For Project Maintainers

1. **Update `cube_board_versions.txt`**: Add Hall sensor info
   ```
   # Cube ID = Board Version (Hall Sensor?)
   1=v1
   2=v6+hall
   3=v1
   4=v6
   5=v1
   6=v1
   ```

2. **Update `flash_cubes.sh`**: Handle Hall sensor variant
   ```bash
   # In flash_cube() function
   if [[ "$version" == *"hall"* ]]; then
       env="v6_with_hall"
   else
       env="$version"
   fi
   ```

3. **Add explicit documentation**: Create a `/tools/FLASHING_GUIDE.md` with:
   - Clear list of which cubes need which firmware
   - Network-based flashing explanation
   - Step-by-step workflow
   - Expected output and verification

### For Safety Skill Implementation

1. **Implement Hall sensor check** before flashing cube 2
2. **Validate network flashing** method upfront
3. **Enforce pre-flight tests** as mandatory step
4. **Add post-flash verification** as completion check
5. **Prefer existing scripts** when available
6. **Cross-reference data sources** for consistency

---

## Test Results Summary

| Issue | Present | Preventable | Impact |
|-------|---------|-------------|--------|
| Hall sensor forgotten | YES | With verification | CRITICAL |
| Serial port assumption | MAYBE | With CLAUDE.md check | MAJOR |
| Script bypass | YES | With explicit preference | MAJOR |
| No pre-flight tests | YES | With mandatory workflow | MINOR |
| No post-flash verify | YES | With verification step | MINOR |

**Bottom Line**: Baseline Claude has **5 safety gaps**. A safety skill can eliminate all of them with proper data validation, context synthesis, and workflow enforcement.
