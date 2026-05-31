#!/usr/bin/env python3
import argparse
import json
import math
import subprocess
import tempfile
from pathlib import Path

import cv2
import numpy as np


W = 640
H = 640
FPS = 30
FRAMES = 120
ROTATION_DEGREES = 5.0
ROTATION_SCALE = 0.92
PAGE_W = 1080
PAGE_H = 1920
PANE_X = (PAGE_W - W) // 2
TOP_Y = 246
BOTTOM_Y = 930

ZONES = [
    ("LOW", 4.0, 0, 0, 320, 320),
    ("MID", 12.0, 320, 0, 640, 320),
    ("HIGH", 24.0, 0, 320, 320, 640),
    ("EXTREME", 40.0, 320, 320, 640, 640),
]


def asset_dir(root: Path) -> Path:
    return root / "assets" / "loop" / "tnr_cl"


def clamp_u8(img: np.ndarray) -> np.ndarray:
    return np.clip(img, 0, 255).astype(np.uint8)


def draw_text(img, text, x, y, scale=0.42, color=(224, 224, 224), thickness=1):
    cv2.putText(img, text, (x, y), cv2.FONT_HERSHEY_SIMPLEX, scale, color,
                thickness, cv2.LINE_AA)


def draw_line_pairs(img, x, y, widths, height=42):
    px = x
    for w in widths:
        draw_text(img, f"{w}px", px, y - 8, 0.32, (210, 210, 210), 1)
        for i in range(10):
            c = (230, 230, 230) if (i % 2 == 0) else (24, 24, 24)
            cv2.rectangle(img, (px + i * w, y),
                          (px + (i + 1) * w - 1, y + height), c, -1)
        px += max(10 * w + 14, 34)


def draw_checker(img, x, y, size, blocks):
    for by in range(blocks):
        for bx in range(blocks):
            c = (226, 226, 226) if ((bx + by) % 2 == 0) else (28, 28, 28)
            cv2.rectangle(img, (x + bx * size, y + by * size),
                          (x + (bx + 1) * size - 1, y + (by + 1) * size - 1),
                          c, -1)


def draw_zone(img, name, sigma, x0, y0, x1, y1):
    color = (150, 210, 255) if sigma <= 12 else (150, 170, 255)
    cv2.rectangle(img, (x0 + 4, y0 + 4), (x1 - 5, y1 - 5), color, 1)
    draw_text(img, f"{name} NOISE  SIGMA {int(sigma)}", x0 + 14, y0 + 25,
              0.48, (236, 236, 236), 1)

    # Slanted edge for sharpness preservation checks.
    sx, sy = x0 + 208, y0 + 54
    cv2.rectangle(img, (sx, sy), (sx + 86, sy + 92), (30, 30, 30), -1)
    pts = np.array([[sx + 22, sy], [sx + 86, sy], [sx + 86, sy + 92],
                    [sx + 48, sy + 92]], np.int32)
    cv2.fillPoly(img, [pts], (226, 226, 226))
    cv2.rectangle(img, (sx, sy), (sx + 86, sy + 92), (110, 110, 110), 1)
    draw_text(img, "EDGE", sx + 6, sy + 108, 0.34, (210, 210, 210), 1)

    draw_line_pairs(img, x0 + 18, y0 + 58, [12, 8, 4, 2, 1], 42)
    draw_text(img, "LINE PAIRS", x0 + 18, y0 + 124, 0.34, (210, 210, 210), 1)

    # Horizontal fine structure.
    hx, hy = x0 + 20, y0 + 146
    for i, step in enumerate([10, 6, 4, 2]):
        yy = hy + i * 22
        for k in range(8):
            c = (225, 225, 225) if k % 2 == 0 else (25, 25, 25)
            cv2.rectangle(img, (hx, yy + k * step), (hx + 122, yy + (k + 1) * step - 1), c, -1)
        draw_text(img, f"H{step}", hx + 132, yy + 16, 0.34, (210, 210, 210), 1)

    draw_checker(img, x0 + 196, y0 + 176, 8, 8)
    draw_checker(img, x0 + 270, y0 + 176, 4, 12)
    draw_text(img, "CHECKER", x0 + 198, y0 + 254, 0.34, (210, 210, 210), 1)

    cv2.circle(img, (x0 + 72, y0 + 262), 34, (222, 222, 222), 2)
    cv2.circle(img, (x0 + 72, y0 + 262), 18, (32, 32, 32), 2)
    cv2.line(img, (x0 + 38, y0 + 262), (x0 + 106, y0 + 262), (222, 222, 222), 1)
    cv2.line(img, (x0 + 72, y0 + 228), (x0 + 72, y0 + 296), (222, 222, 222), 1)
    draw_text(img, "ABC123", x0 + 126, y0 + 286, 0.56, (232, 232, 232), 1)

    ramp_x, ramp_y, ramp_w = x0 + 18, y1 - 28, 282
    ramp = np.linspace(36, 216, ramp_w, dtype=np.uint8)
    img[ramp_y:ramp_y + 12, ramp_x:ramp_x + ramp_w] = ramp.reshape(1, -1, 1)
    cv2.rectangle(img, (ramp_x, ramp_y), (ramp_x + ramp_w - 1, ramp_y + 11), (160, 160, 160), 1)


def clean_frame() -> np.ndarray:
    yy, xx = np.mgrid[0:H, 0:W]
    base = 42 + (xx * 58 // W) + (yy * 42 // H)
    img = np.dstack([base, base, base]).astype(np.uint8)
    cv2.rectangle(img, (0, 0), (W - 1, H - 1), (190, 190, 190), 2)
    cv2.line(img, (W // 2, 0), (W // 2, H), (90, 90, 90), 1)
    cv2.line(img, (0, H // 2), (W, H // 2), (90, 90, 90), 1)
    draw_text(img, "TNR CONTENT PRESERVATION CHART", 116, 314, 0.48, (238, 238, 238), 1)
    for zone in ZONES:
        draw_zone(img, *zone)
    return img


def rotation_angle(frame_idx: int) -> float:
    return ROTATION_DEGREES * math.sin(2.0 * math.pi * frame_idx / FRAMES)


def move_content(clean: np.ndarray, frame_idx: int) -> np.ndarray:
    angle = rotation_angle(frame_idx)
    mat = cv2.getRotationMatrix2D((W / 2.0, H / 2.0), angle, ROTATION_SCALE)
    return cv2.warpAffine(clean, mat, (W, H), flags=cv2.INTER_LINEAR,
                          borderMode=cv2.BORDER_CONSTANT,
                          borderValue=(42, 42, 42))


def apply_zone_noise(clean: np.ndarray, frame_idx: int) -> np.ndarray:
    noisy = clean.astype(np.float32)
    rng = np.random.default_rng(0x3456 + frame_idx * 977)
    for _, sigma, x0, y0, x1, y1 in ZONES:
        h = y1 - y0
        w = x1 - x0
        coarse_h = max(1, h // 4)
        coarse_w = max(1, w // 4)
        coarse = rng.normal(0.0, sigma, (coarse_h, coarse_w, 1)).astype(np.float32)
        luma = cv2.resize(coarse, (w, h), interpolation=cv2.INTER_NEAREST).reshape(h, w, 1)
        fine = rng.normal(0.0, sigma * 0.18, (h, w, 1))
        chroma = rng.normal(0.0, sigma * 0.06, (h, w, 3))
        luma = luma + fine
        noisy[y0:y1, x0:x1] += luma + chroma
        if sigma >= 24:
            mask = rng.random((h, w, 1)) < (0.0015 if sigma < 40 else 0.0035)
            impulse = rng.choice([-70.0, 70.0], size=(h, w, 1))
            noisy[y0:y1, x0:x1] += mask * impulse
    return clamp_u8(noisy)


def bgr_to_nv12(img: np.ndarray) -> bytes:
    i420 = cv2.cvtColor(img, cv2.COLOR_BGR2YUV_I420).reshape(-1)
    y_size = W * H
    uv_size = y_size // 4
    y = i420[:y_size]
    u = i420[y_size:y_size + uv_size]
    v = i420[y_size + uv_size:y_size + uv_size * 2]
    uv = np.empty(uv_size * 2, dtype=np.uint8)
    uv[0::2] = u
    uv[1::2] = v
    return y.tobytes() + uv.tobytes()


def generate(root: Path, keep_nv12: bool = False) -> None:
    out_dir = asset_dir(root)
    out_dir.mkdir(parents=True, exist_ok=True)
    h264_path = out_dir / "online_lowlight_noisy_640x640_120.h264"
    clean_png = out_dir / "tnr_zone_chart_clean_reference.png"
    noisy_png = out_dir / "tnr_zone_chart_noisy_reference.png"
    zones_json = out_dir / "tnr_zone_chart_zones.json"
    clean = clean_frame()
    clean0 = move_content(clean, 0)
    noisy0 = apply_zone_noise(clean0, 0)
    cv2.imwrite(str(clean_png), clean0)
    cv2.imwrite(str(noisy_png), noisy0)
    zones_json.write_text(json.dumps({
        "width": W,
        "height": H,
        "frames": FRAMES,
        "fps": FPS,
        "motion": {
            "type": "sinusoidal_rotation",
            "amplitude_degrees": ROTATION_DEGREES,
            "scale": ROTATION_SCALE,
            "period_frames": FRAMES
        },
        "zones": [
            {"name": n, "sigma": s, "x0": x0, "y0": y0, "x1": x1, "y1": y1}
            for n, s, x0, y0, x1, y1 in ZONES
        ],
        "h264": str(h264_path.relative_to(root)),
        "clean_reference": str(clean_png.relative_to(root)),
        "noisy_reference": str(noisy_png.relative_to(root)),
    }, indent=2) + "\n")

    temp_path = None
    try:
        with tempfile.NamedTemporaryFile(prefix="tnr_zone_", suffix=".nv12", delete=False) as fp:
            temp_path = Path(fp.name)
            for frame_idx in range(FRAMES):
                fp.write(bgr_to_nv12(apply_zone_noise(move_content(clean, frame_idx), frame_idx)))
        cmd = [
            "ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
            "-f", "rawvideo", "-pix_fmt", "nv12", "-s:v", f"{W}x{H}",
            "-r", str(FPS), "-i", str(temp_path),
            "-c:v", "libx264", "-profile:v", "baseline",
            "-preset", "veryfast", "-threads", "1",
            "-bf", "0", "-refs", "1", "-g", "30", "-keyint_min", "30",
            "-sc_threshold", "0", "-crf", "24",
            "-pix_fmt", "yuv420p", "-f", "h264", str(h264_path),
        ]
        subprocess.run(cmd, check=True)
        if keep_nv12:
            noisy_nv12 = out_dir / "tnr_zone_chart_noisy_640x640_120.nv12"
            temp_path.replace(noisy_nv12)
            temp_path = None
    finally:
        if temp_path and temp_path.exists():
            temp_path.unlink()
    print(f"generated: {h264_path}")
    print(f"reference: {clean_png}")
    print(f"reference: {noisy_png}")


def crop_pane(capture: np.ndarray, top_y: int) -> np.ndarray:
    ch, cw = capture.shape[:2]
    sx = cw / float(PAGE_W)
    sy = ch / float(PAGE_H)
    x = int(round(PANE_X * sx))
    y = int(round(top_y * sy))
    w = int(round(W * sx))
    h = int(round(H * sy))
    crop = capture[y:y + h, x:x + w]
    if crop.size == 0:
        raise ValueError("capture crop is empty")
    return cv2.resize(crop, (W, H), interpolation=cv2.INTER_AREA)


def psnr(ref, test):
    mse = np.mean((ref.astype(np.float32) - test.astype(np.float32)) ** 2)
    if mse <= 1e-9:
        return 99.0
    return 20.0 * math.log10(255.0 / math.sqrt(mse))


def zone_metrics(clean_y, input_y, output_y):
    rows = []
    gx = cv2.Sobel(clean_y, cv2.CV_32F, 1, 0, ksize=3)
    gy = cv2.Sobel(clean_y, cv2.CV_32F, 0, 1, ksize=3)
    clean_grad = cv2.magnitude(gx, gy)
    in_grad = cv2.magnitude(cv2.Sobel(input_y, cv2.CV_32F, 1, 0, ksize=3),
                            cv2.Sobel(input_y, cv2.CV_32F, 0, 1, ksize=3))
    out_grad = cv2.magnitude(cv2.Sobel(output_y, cv2.CV_32F, 1, 0, ksize=3),
                             cv2.Sobel(output_y, cv2.CV_32F, 0, 1, ksize=3))
    for name, sigma, x0, y0, x1, y1 in ZONES:
        c = clean_y[y0:y1, x0:x1].astype(np.float32)
        n = input_y[y0:y1, x0:x1].astype(np.float32)
        o = output_y[y0:y1, x0:x1].astype(np.float32)
        rin = n - c
        rout = o - c
        edge = clean_grad[y0:y1, x0:x1] > 45.0
        cg = clean_grad[y0:y1, x0:x1][edge]
        ig = in_grad[y0:y1, x0:x1][edge]
        og = out_grad[y0:y1, x0:x1][edge]
        clean_edge = float(np.mean(cg)) if cg.size else 0.0
        rows.append({
            "zone": name,
            "target_sigma": sigma,
            "input_residual_std": float(np.std(rin)),
            "output_residual_std": float(np.std(rout)),
            "noise_reduction_pct": float((1.0 - (np.std(rout) / max(np.std(rin), 1e-6))) * 100.0),
            "input_psnr": psnr(c, n),
            "output_psnr": psnr(c, o),
            "psnr_gain": psnr(c, o) - psnr(c, n),
            "input_edge_ratio": float(np.mean(ig) / clean_edge) if clean_edge else 0.0,
            "output_edge_ratio": float(np.mean(og) / clean_edge) if clean_edge else 0.0,
            "edge_keep_vs_input_pct": float((np.mean(og) / max(np.mean(ig), 1e-6)) * 100.0) if ig.size else 0.0,
        })
    return rows


def evaluate(root: Path, capture_path: Path, out_dir: Path) -> None:
    ref_path = asset_dir(root) / "tnr_zone_chart_clean_reference.png"
    if not ref_path.exists():
        raise FileNotFoundError(f"missing clean reference: {ref_path}")
    capture = cv2.imread(str(capture_path), cv2.IMREAD_COLOR)
    clean = cv2.imread(str(ref_path), cv2.IMREAD_COLOR)
    if capture is None or clean is None:
        raise ValueError("failed to read capture or reference")
    inp = crop_pane(capture, TOP_Y)
    out = crop_pane(capture, BOTTOM_Y)
    clean_y = cv2.cvtColor(clean, cv2.COLOR_BGR2GRAY)
    input_y = cv2.cvtColor(inp, cv2.COLOR_BGR2GRAY)
    output_y = cv2.cvtColor(out, cv2.COLOR_BGR2GRAY)
    rows = zone_metrics(clean_y, input_y, output_y)
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / "tnr_eval.json").write_text(json.dumps(rows, indent=2) + "\n")
    with (out_dir / "tnr_eval.tsv").open("w") as fp:
        fp.write("zone\ttarget_sigma\tinput_std\toutput_std\tnoise_reduction_pct\tinput_psnr\toutput_psnr\tpsnr_gain\tinput_edge_ratio\toutput_edge_ratio\tedge_keep_vs_input_pct\n")
        for r in rows:
            fp.write(
                f"{r['zone']}\t{r['target_sigma']:.0f}\t"
                f"{r['input_residual_std']:.2f}\t{r['output_residual_std']:.2f}\t"
                f"{r['noise_reduction_pct']:.1f}\t{r['input_psnr']:.2f}\t"
                f"{r['output_psnr']:.2f}\t{r['psnr_gain']:.2f}\t"
                f"{r['input_edge_ratio']:.2f}\t{r['output_edge_ratio']:.2f}\t"
                f"{r['edge_keep_vs_input_pct']:.1f}\n"
            )
    preview = np.vstack([clean, inp, out])
    draw_text(preview, "CLEAN REFERENCE", 12, 28, 0.65, (60, 240, 240), 2)
    draw_text(preview, "CAPTURED NOISY INPUT", 12, H + 28, 0.65, (60, 240, 240), 2)
    draw_text(preview, "CAPTURED TNR OUTPUT", 12, H * 2 + 28, 0.65, (60, 240, 240), 2)
    cv2.imwrite(str(out_dir / "tnr_eval_preview.png"), preview)
    print(out_dir / "tnr_eval.tsv")
    for r in rows:
        print(f"{r['zone']:7s} input_std={r['input_residual_std']:.2f} "
              f"output_std={r['output_residual_std']:.2f} "
              f"reduction={r['noise_reduction_pct']:.1f}% "
              f"edge_keep={r['edge_keep_vs_input_pct']:.1f}%")


def evaluate_recording(video_path: Path, out_dir: Path, skip_frames: int, max_frames: int) -> None:
    cap = cv2.VideoCapture(str(video_path))
    if not cap.isOpened():
        raise ValueError(f"failed to open recording: {video_path}")

    input_frames = []
    output_frames = []
    frame_idx = 0
    while True:
        ok, frame = cap.read()
        if not ok:
            break
        if frame_idx >= skip_frames:
            inp = cv2.cvtColor(crop_pane(frame, TOP_Y), cv2.COLOR_BGR2GRAY)
            out = cv2.cvtColor(crop_pane(frame, BOTTOM_Y), cv2.COLOR_BGR2GRAY)
            input_frames.append(inp)
            output_frames.append(out)
            if len(input_frames) >= max_frames:
                break
        frame_idx += 1
    cap.release()

    if len(input_frames) < 8:
        raise ValueError(f"not enough decoded frames for temporal eval: {len(input_frames)}")

    in_stack = np.stack(input_frames).astype(np.float32)
    out_stack = np.stack(output_frames).astype(np.float32)
    in_temporal = np.std(in_stack, axis=0)
    out_temporal = np.std(out_stack, axis=0)

    rows = []
    for name, sigma, x0, y0, x1, y1 in ZONES:
        in_std = float(np.mean(in_temporal[y0:y1, x0:x1]))
        out_std = float(np.mean(out_temporal[y0:y1, x0:x1]))
        rows.append({
            "zone": name,
            "target_sigma": sigma,
            "input_temporal_std": in_std,
            "output_temporal_std": out_std,
            "temporal_reduction_pct": float((1.0 - out_std / max(in_std, 1e-6)) * 100.0),
        })

    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / "tnr_temporal_eval.json").write_text(json.dumps({
        "video": str(video_path),
        "decoded_frames": len(input_frames),
        "skip_frames": skip_frames,
        "rows": rows,
    }, indent=2) + "\n")
    with (out_dir / "tnr_temporal_eval.tsv").open("w") as fp:
        fp.write("zone\ttarget_sigma\tinput_temporal_std\toutput_temporal_std\ttemporal_reduction_pct\n")
        for r in rows:
            fp.write(
                f"{r['zone']}\t{r['target_sigma']:.0f}\t"
                f"{r['input_temporal_std']:.2f}\t{r['output_temporal_std']:.2f}\t"
                f"{r['temporal_reduction_pct']:.1f}\n"
            )

    input_mean = np.mean(in_stack, axis=0).astype(np.uint8)
    output_mean = np.mean(out_stack, axis=0).astype(np.uint8)
    input_std_vis = np.clip(in_temporal * 5.0, 0, 255).astype(np.uint8)
    output_std_vis = np.clip(out_temporal * 5.0, 0, 255).astype(np.uint8)
    preview = np.vstack([
        cv2.cvtColor(input_mean, cv2.COLOR_GRAY2BGR),
        cv2.cvtColor(output_mean, cv2.COLOR_GRAY2BGR),
        cv2.applyColorMap(input_std_vis, cv2.COLORMAP_INFERNO),
        cv2.applyColorMap(output_std_vis, cv2.COLORMAP_INFERNO),
    ])
    draw_text(preview, "INPUT TEMPORAL MEAN", 12, 28, 0.65, (60, 240, 240), 2)
    draw_text(preview, "TNR OUTPUT TEMPORAL MEAN", 12, H + 28, 0.65, (60, 240, 240), 2)
    draw_text(preview, "INPUT TEMPORAL STD X5", 12, H * 2 + 28, 0.65, (60, 240, 240), 2)
    draw_text(preview, "TNR OUTPUT TEMPORAL STD X5", 12, H * 3 + 28, 0.65, (60, 240, 240), 2)
    cv2.imwrite(str(out_dir / "tnr_temporal_eval_preview.png"), preview)

    print(out_dir / "tnr_temporal_eval.tsv")
    print(f"decoded_frames={len(input_frames)} skip_frames={skip_frames}")
    for r in rows:
        print(f"{r['zone']:7s} input_tstd={r['input_temporal_std']:.2f} "
              f"output_tstd={r['output_temporal_std']:.2f} "
              f"temporal_reduction={r['temporal_reduction_pct']:.1f}%")


def main() -> None:
    parser = argparse.ArgumentParser(description="Generate and evaluate TNR zone-noise chart assets.")
    parser.add_argument("--root", default=Path(__file__).resolve().parents[1], type=Path)
    sub = parser.add_subparsers(dest="cmd")
    gen = sub.add_parser("generate")
    gen.add_argument("--keep-nv12", action="store_true")
    ev = sub.add_parser("evaluate-capture")
    ev.add_argument("capture", type=Path)
    ev.add_argument("--out-dir", type=Path)
    evr = sub.add_parser("evaluate-recording")
    evr.add_argument("video", type=Path)
    evr.add_argument("--out-dir", type=Path)
    evr.add_argument("--skip-frames", type=int, default=30)
    evr.add_argument("--max-frames", type=int, default=120)
    args = parser.parse_args()

    root = args.root.resolve()
    if args.cmd in (None, "generate"):
        generate(root, getattr(args, "keep_nv12", False))
    elif args.cmd == "evaluate-capture":
        out_dir = args.out_dir or args.capture.parent
        evaluate(root, args.capture, out_dir)
    elif args.cmd == "evaluate-recording":
        out_dir = args.out_dir or args.video.parent
        evaluate_recording(args.video, out_dir, args.skip_frames, args.max_frames)


if __name__ == "__main__":
    main()
