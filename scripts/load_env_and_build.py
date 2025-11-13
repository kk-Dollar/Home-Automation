#!/usr/bin/env python3
"""
Simple script to load key=value pairs from a .env file into the environment
and then run PlatformIO (`pio`) with the provided arguments.

Usage:
  python scripts/load_env_and_build.py run            # runs a build
  python scripts/load_env_and_build.py run -e esp32dev # pass-through args to pio
  python scripts/load_env_and_build.py upload

This avoids committing secrets to git; the .env file is ignored by .gitignore.
"""
import os
import sys
import subprocess
from pathlib import Path


def load_dotenv(path: Path):
    if not path.exists():
        return
    with path.open("r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            if "=" not in line:
                continue
            key, val = line.split("=", 1)
            key = key.strip()
            val = val.strip().strip('"').strip("'")
            # Do not overwrite already set environment variables
            if key not in os.environ:
                os.environ[key] = val


def find_pio_cmd():
    # prefer pio but fall back to platformio
    from shutil import which

    for cmd in ("pio", "platformio"):
        if which(cmd):
            return cmd
    return None


def main(argv):
    repo_root = Path(__file__).resolve().parents[1]
    dotenv = repo_root / ".env"
    load_dotenv(dotenv)

    pio = find_pio_cmd()
    if not pio:
        print("Error: could not find 'pio' or 'platformio' in PATH. Install PlatformIO CLI.")
        sys.exit(2)

    if len(argv) == 0:
        argv = ["run"]

    cmd = [pio] + argv
    print("Running:", " ".join(cmd))
    # Inherit current environment (with loaded .env)
    try:
        result = subprocess.run(cmd, env=os.environ)
        sys.exit(result.returncode)
    except KeyboardInterrupt:
        raise
    except Exception as exc:
        print("Failed to run PlatformIO:", exc)
        sys.exit(3)


if __name__ == "__main__":
    main(sys.argv[1:])
