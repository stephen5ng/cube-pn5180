import subprocess
from datetime import datetime
import os
Import("env")

# Ensure git commands run from project directory
project_dir = env['PROJECT_DIR']

# Check for uncommitted changes in src folder only (ignore submodule errors)
result = subprocess.run(["git", "status", "--porcelain", "--ignore-submodules", "src"],
                       cwd=project_dir, capture_output=True, text=True)
is_dirty = len(result.stdout.strip()) > 0

if is_dirty:
    # Uncommitted changes exist - use build timestamp to force flash
    build_ts = str(int(datetime.now().timestamp()))
    version = build_ts
else:
    # Clean working tree - use git SHA for reproducible builds
    version = subprocess.check_output(["git", "rev-parse", "--short", "HEAD"],
                                    cwd=project_dir).decode().strip()

env.Append(CPPDEFINES=[("GIT_VERSION", env.StringifyMacro(version))])

# Commit timestamp in MMDD.HHMM format for display on boot
# Append "u" if there are uncommitted changes (experimental build)
ts = subprocess.check_output(["git", "log", "-1", "--format=%cd", "--date=format:%m%d.%H%M"],
                           cwd=project_dir).decode().strip()
if is_dirty:
    ts = ts + "u"
env.Append(CPPDEFINES=[("GIT_TIMESTAMP", env.StringifyMacro(ts))])
