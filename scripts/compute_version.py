#!/usr/bin/env python3
"""Compute firmware version string.

Format:
  <sha>+<env>                  when src/ is clean
  <sha>-<src_hash>+<env>       when src/ is dirty (sha is HEAD, src_hash hashes
                               the current contents of src/ so identical-source
                               rebuilds produce the same version)

Run as a script to print the version for a given env:
  python3 compute_version.py <env_name> [project_dir]
"""
import hashlib
import os
import subprocess
import sys
from pathlib import Path


def is_src_dirty(project_dir):
    result = subprocess.run(
        ["git", "status", "--porcelain", "--ignore-submodules", "src"],
        cwd=project_dir, capture_output=True, text=True, check=True,
    )
    return bool(result.stdout.strip())


def src_hash(project_dir):
    h = hashlib.sha1()
    src = Path(project_dir) / "src"
    for path in sorted(p for p in src.rglob("*") if p.is_file()):
        h.update(path.relative_to(project_dir).as_posix().encode())
        h.update(b"\0")
        h.update(path.read_bytes())
    return h.hexdigest()[:7]


def compute_version(env_name, project_dir):
    sha = subprocess.check_output(
        ["git", "rev-parse", "--short", "HEAD"], cwd=project_dir, text=True
    ).strip()
    if is_src_dirty(project_dir):
        return f"{sha}-{src_hash(project_dir)}+{env_name}"
    return f"{sha}+{env_name}"


if __name__ == "__main__":
    env_name = sys.argv[1]
    project_dir = sys.argv[2] if len(sys.argv) > 2 else os.getcwd()
    print(compute_version(env_name, project_dir))
