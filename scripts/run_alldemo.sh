#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

RKTOHI_ROOT="${RKTOHI_ROOT:-/userdata/rktohi}"
SYNC_CHANGED=0

sync_file() {
    local src="$1"
    local dst="$2"
    local mode="$3"

    if [[ ! -f "$src" ]]; then
        echo "missing rktohi artifact: $src" >&2
        exit 1
    fi

    if [[ ! -f "$dst" ]] || ! cmp -s "$src" "$dst"; then
        mkdir -p "$(dirname "$dst")"
        install -m "$mode" "$src" "$dst"
        SYNC_CHANGED=1
        echo "synced $dst"
    fi
}

sync_rktohi_artifacts() {
    sync_file "$RKTOHI_ROOT/include/media_api.h" "include/media_api.h" 0644
    sync_file "$RKTOHI_ROOT/build/libmedia.a" "lib/libmedia.a" 0644
    sync_file "$RKTOHI_ROOT/build/libmedia.so" "lib/libmedia.so" 0755
}

build_if_needed() {
    if [[ ! -f build/Makefile && ! -f build/build.ninja ]]; then
        cmake -S "$PWD" -B "$PWD/build"
    fi

    local src_changed=0
    if [[ -x build/alldemo ]]; then
        if [[ CMakeLists.txt -nt build/alldemo ||
              include/media_api.h -nt build/alldemo ||
              lib/libmedia.a -nt build/alldemo ]]; then
            src_changed=1
        else
            local newer_src
            newer_src="$(find src -type f -newer build/alldemo -print -quit)"
            if [[ -n "$newer_src" ]]; then
                src_changed=1
            fi
        fi
    fi

    if [[ "$SYNC_CHANGED" -ne 0 || "$src_changed" -ne 0 || ! -x build/alldemo ]]; then
        local jobs
        jobs="${BUILD_JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}"
        cmake --build "$PWD/build" -j "$jobs"
    fi
}

sync_rktohi_artifacts
build_if_needed

export LD_LIBRARY_PATH="$PWD/lib:${LD_LIBRARY_PATH:-}"
exec "$PWD/build/alldemo" "$@"
