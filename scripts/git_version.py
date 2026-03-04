import subprocess
Import("env")

sha = subprocess.check_output(["git", "rev-parse", "--short", "HEAD"]).decode().strip()
env.Append(CPPDEFINES=[("GIT_VERSION", env.StringifyMacro(sha))])

# Commit timestamp in MMDD.HHMM format for display on boot
ts = subprocess.check_output(["git", "log", "-1", "--format=%cd", "--date=format:%m%d.%H%M"]).decode().strip()
env.Append(CPPDEFINES=[("GIT_TIMESTAMP", env.StringifyMacro(ts))])
