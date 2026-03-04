import subprocess
Import("env")

sha = subprocess.check_output(["git", "rev-parse", "--short", "HEAD"]).decode().strip()
env.Append(CPPDEFINES=[("GIT_VERSION", env.StringifyMacro(sha))])
