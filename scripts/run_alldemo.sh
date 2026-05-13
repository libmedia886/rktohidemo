#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."
export LD_LIBRARY_PATH="$PWD/lib:${LD_LIBRARY_PATH:-}"
export DISPLAY=:0
exec "$PWD/build/alldemo" "$@"
