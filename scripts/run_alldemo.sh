#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."
export LD_LIBRARY_PATH="$PWD/lib:${LD_LIBRARY_PATH:-}"
if [[ -S /tmp/.X11-unix/X0 ]]; then
  export DISPLAY=:0
fi
exec "$PWD/build/alldemo" "$@"
