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

sync_rktohi_demo_assets() {
    local video_root="$RKTOHI_ROOT/research/avm_2d_parking/data/prepared/avm3d_video"
    local output_root="$RKTOHI_ROOT/research/avm_2d_parking/outputs/dyfcalid"
    local retinex_root="$RKTOHI_ROOT/research/retinex/normalized/exdark_boat"
    local retinex_dst="assets/loop/retinex/exdark"
    local frame

    for frame in $(seq -f "%03g" 0 24); do
        sync_file "$video_root/avm3d_video_$frame/front.jpg" "assets/loop/avm2d/video/$frame/front.jpg" 0644
        sync_file "$video_root/avm3d_video_$frame/rear.jpg" "assets/loop/avm2d/video/$frame/rear.jpg" 0644
        sync_file "$video_root/avm3d_video_$frame/left.jpg" "assets/loop/avm2d/video/$frame/left.jpg" 0644
        sync_file "$video_root/avm3d_video_$frame/right.jpg" "assets/loop/avm2d/video/$frame/right.jpg" 0644
    done
    sync_file "$output_root/surround_blend_1_balance_1_car_1.jpg" "assets/loop/avm2d/dyfcalid/surround_blend_1_balance_1_car_1.jpg" 0644
    sync_file "$output_root/avm_gpu_output_overlay.jpg" "assets/loop/avm2d/dyfcalid/avm_gpu_output_overlay.jpg" 0644

    if [[ -d "$retinex_root" ]]; then
        local sample_dir
        local idx=0
        while IFS= read -r sample_dir; do
            if [[ "$idx" -ge 100 ]]; then
                break
            fi

            local input=""
            local ext
            for ext in jpg jpeg png JPG JPEG PNG; do
                if [[ -f "$sample_dir/input.$ext" ]]; then
                    input="$sample_dir/input.$ext"
                    break
                fi
            done
            if [[ -z "$input" ]]; then
                continue
            fi

            idx=$((idx + 1))
            local base
            local out_ext
            local dst
            base="$(basename "$sample_dir")"
            out_ext="${input##*.}"
            printf -v dst "%s/%03d_%s.%s" "$retinex_dst" "$idx" "$base" "$out_ext"
            sync_file "$input" "$dst" 0644
        done < <(find "$retinex_root" -mindepth 1 -maxdepth 1 -type d | sort)

        if [[ "$idx" -lt 100 ]]; then
            echo "warning: synced only $idx retinex exdark samples from $retinex_root" >&2
        fi
    else
        echo "warning: missing retinex exdark root: $retinex_root" >&2
    fi
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
sync_rktohi_demo_assets
build_if_needed

export LD_LIBRARY_PATH="$PWD/lib:${LD_LIBRARY_PATH:-}"
exec "$PWD/build/alldemo" "$@"
