#!/usr/bin/env bash
# Small wrapper to run the Python loader then pio with arguments.
# Usage: ./scripts/build.sh run

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
python3 "$SCRIPT_DIR/load_env_and_build.py" "$@"
