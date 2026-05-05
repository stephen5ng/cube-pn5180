import os
import sys
import subprocess
from datetime import datetime

Import("env")

project_dir = env['PROJECT_DIR']
env_name = env['PIOENV']

sys.path.insert(0, os.path.join(project_dir, 'scripts'))
from compute_version import compute_version, is_src_dirty

version = compute_version(env_name, project_dir)
env.Append(CPPDEFINES=[("GIT_VERSION", env.StringifyMacro(version))])

# Boot timestamp (MMDD.HHMM) shown on the display:
# - clean tree: commit time, so cubes show when this firmware was tagged
# - dirty tree: build time + "u", so each iteration shows when it was compiled
if is_src_dirty(project_dir):
    ts = datetime.now().strftime("%m%d.%H%M") + "u"
else:
    ts = subprocess.check_output(
        ["git", "log", "-1", "--format=%cd", "--date=format:%m%d.%H%M"],
        cwd=project_dir,
    ).decode().strip()
env.Append(CPPDEFINES=[("GIT_TIMESTAMP", env.StringifyMacro(ts))])
