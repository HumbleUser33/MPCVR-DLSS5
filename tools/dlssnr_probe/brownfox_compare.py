"""What each chroma upsampler makes of the doom9 BrownFox test.

The test (forum.doom9.org, madVR chroma comparison) is one picture in two
encodes: BrownFox 444 as the truth, BrownFox 420 as what a player receives, and
one PNG per algorithm as madVR rendered it. Red text with a black outline on
white is where chroma upsampling shows: bleeding across the outline, and
overshoot past the colours that are really there.

This measures every PNG of the folder against the 4:4:4 truth, so that madVR's
algorithms and ours land on the same scale. Ours are written out by the harness
(dlssnr_harness --tchroma --save) into the same folder shape.

    python brownfox_compare.py "upscale_refs\\Chroma test"

Needs ffmpeg on the path (to read the PNGs) and numpy.
"""

import os
import subprocess
import sys

import numpy as np


def read_png(path):
    """The picture as float RGB in 0..1, through ffmpeg."""
    probe = subprocess.run(
        ["ffprobe", "-v", "error", "-select_streams", "v:0",
         "-show_entries", "stream=width,height", "-of", "csv=p=0:s=x", path],
        capture_output=True, text=True, check=True)
    w, h = (int(v) for v in probe.stdout.strip().split("x"))
    raw = subprocess.run(
        ["ffmpeg", "-v", "error", "-i", path, "-f", "rawvideo", "-pix_fmt", "rgb24", "-"],
        capture_output=True, check=True).stdout
    return np.frombuffer(raw, dtype=np.uint8).reshape(h, w, 3).astype(np.float32) / 255.0


def to_ycbcr(rgb):
    """BT.709, full range: what the picture is made of, as the renderer sees it."""
    r, g, b = rgb[..., 0], rgb[..., 1], rgb[..., 2]
    y = 0.2126 * r + 0.7152 * g + 0.0722 * b
    cb = (b - y) / 1.8556
    cr = (r - y) / 1.5748
    return y, cb, cr


def psnr(se, n):
    mse = se / max(n, 1)
    return 10.0 * np.log10(1.0 / mse) if mse > 0 else 99.0


def local_range(plane, radius=1):
    """The lowest and the highest the reference really goes, around each pixel."""
    lo = plane.copy()
    hi = plane.copy()
    for dy in range(-radius, radius + 1):
        for dx in range(-radius, radius + 1):
            shifted = np.roll(np.roll(plane, dy, axis=0), dx, axis=1)
            lo = np.minimum(lo, shifted)
            hi = np.maximum(hi, shifted)
    return lo, hi


def measure(test, ref):
    """Every number one picture earns against the truth."""
    y, cb, cr = to_ycbcr(test)
    ry, rcb, rcr = to_ycbcr(ref)

    n = ref.shape[0] * ref.shape[1]
    out = {}
    out["psnr-rgb"] = psnr(float(np.sum((test - ref) ** 2)), 3 * n)
    out["psnr-c"] = psnr(float(np.sum((cb - rcb) ** 2) + np.sum((cr - rcr) ** 2)), 2 * n)

    # Where the luma has an edge is where chroma bleeding shows.
    gx = np.abs(np.diff(ry, axis=1, prepend=ry[:, :1]))
    gy = np.abs(np.diff(ry, axis=0, prepend=ry[:1, :]))
    edges = (gx + gy) > 0.08
    edge_count = int(np.count_nonzero(edges))
    if edge_count:
        se = np.sum((cb - rcb)[edges] ** 2) + np.sum((cr - rcr)[edges] ** 2)
        out["psnr-e"] = psnr(float(se), 2 * edge_count)
    else:
        out["psnr-e"] = 99.0

    # Ringing: how far past the colours really present around it a pixel goes,
    # in 8-bit levels, averaged over the pixels that overshoot at all.
    over = np.zeros_like(cb)
    for plane, rplane in ((cb, rcb), (cr, rcr)):
        lo, hi = local_range(rplane)
        over += np.maximum(plane - hi, 0.0) + np.maximum(lo - plane, 0.0)
    ringing = over[edges] if edge_count else over
    out["ring"] = float(np.mean(ringing)) * 255.0

    # And the same for the luma, which is what a doubler overshoots on.
    lo, hi = local_range(ry)
    over_y = np.maximum(y - hi, 0.0) + np.maximum(lo - y, 0.0)
    out["ring-y"] = float(np.mean(over_y[edges] if edge_count else over_y)) * 255.0

    # Sharpness, as the gradient the picture carries against the truth's.
    ref_grad = float(np.mean(np.abs(np.diff(ry, axis=1))) + np.mean(np.abs(np.diff(ry, axis=0))))
    test_grad = float(np.mean(np.abs(np.diff(y, axis=1))) + np.mean(np.abs(np.diff(y, axis=0))))
    out["sharp"] = test_grad / ref_grad if ref_grad > 0 else 0.0
    return out


def run(folder):
    sets = [("BrownFox 444.png", "BrownFox 420 ", "at 1:1, chroma upsampling alone"),
            ("BrownFox 2x 444.png", "BrownFox 2x 420 ", "at 2x, chroma and picture together")]

    for truth_name, prefix, what in sets:
        truth_path = os.path.join(folder, truth_name)
        if not os.path.exists(truth_path):
            print("missing:", truth_path)
            continue
        ref = read_png(truth_path)
        names = sorted(f for f in os.listdir(folder)
                       if f.startswith(prefix) and f.lower().endswith(".png"))
        if not names:
            continue

        print("\n%s -- %d x %d, %s" % (truth_name, ref.shape[1], ref.shape[0], what))
        print("  %-28s %8s %8s %8s %7s %7s %7s" %
              ("method", "psnr-c", "psnr-e", "psnr-rgb", "ring", "ring-y", "sharp"))
        rows = []
        for name in names:
            test = read_png(os.path.join(folder, name))
            if test.shape != ref.shape:
                print("  %-28s different size, skipped" % name)
                continue
            m = measure(test, ref)
            rows.append((m["psnr-e"], name[len(prefix):-4], m))
        for _, label, m in sorted(rows, reverse=True):
            print("  %-28s %8.2f %8.2f %8.2f %7.3f %7.3f %7.3f" %
                  (label, m["psnr-c"], m["psnr-e"], m["psnr-rgb"], m["ring"], m["ring-y"], m["sharp"]))


if __name__ == "__main__":
    run(sys.argv[1] if len(sys.argv) > 1 else os.path.join("upscale_refs", "Chroma test"))
