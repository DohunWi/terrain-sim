#!/usr/bin/env python3
"""Side-by-side PGM comparison viewer for terrain-sim heightmap dumps.

core/ writes plain-text (P2) PGM files after each generation/erosion pass
(before.pgm, after.pgm). This renders two or more of them next to each
other, upscaled with nearest-neighbor so individual cells stay crisp, with
each panel labeled by filename and its pixel-value sum/min/max.

Usage:
    python3 tools/visualize_pgm.py core/before.pgm core/after.pgm
    python3 tools/visualize_pgm.py core/before.pgm core/after.pgm --scale 8 --out /tmp/compare.png
"""

import argparse
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

LABEL_HEIGHT = 36
PADDING = 12


def read_pgm_p2(path: Path) -> tuple[int, int, list[int]]:
    tokens = []
    with open(path) as f:
        for line in f:
            line = line.split("#", 1)[0]  # strip PGM comments
            tokens.extend(line.split())

    if tokens[0] != "P2":
        raise ValueError(f"{path}: not a plain-text (P2) PGM")

    width, height, maxval = int(tokens[1]), int(tokens[2]), int(tokens[3])
    pixels = [int(t) for t in tokens[4 : 4 + width * height]]
    if len(pixels) != width * height:
        raise ValueError(f"{path}: expected {width * height} pixels, got {len(pixels)}")
    return width, height, pixels


def panel_image(width: int, height: int, pixels: list[int], scale: int) -> Image.Image:
    img = Image.new("L", (width, height))
    img.putdata(pixels)
    return img.resize((width * scale, height * scale), Image.NEAREST)


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("pgm_files", nargs="+", type=Path, help="two or more .pgm files to compare")
    ap.add_argument("--labels", nargs="*", default=None, help="panel labels (defaults to filenames)")
    ap.add_argument("--scale", type=int, default=6, help="pixel upscale factor (default: 6)")
    ap.add_argument("--out", type=Path, default=Path("compare.png"), help="output PNG path")
    args = ap.parse_args()

    if args.labels and len(args.labels) != len(args.pgm_files):
        ap.error("--labels count must match number of pgm_files")
    labels = args.labels or [p.name for p in args.pgm_files]

    panels = []
    for path in args.pgm_files:
        width, height, pixels = read_pgm_p2(path)
        total, lo, hi = sum(pixels), min(pixels), max(pixels)
        panels.append((panel_image(width, height, pixels, args.scale), total, lo, hi))

    panel_w = max(p[0].width for p in panels)
    panel_h = max(p[0].height for p in panels)
    canvas_w = panel_w * len(panels) + PADDING * (len(panels) + 1)
    canvas_h = panel_h + LABEL_HEIGHT + PADDING * 2

    canvas = Image.new("RGB", (canvas_w, canvas_h), "white")
    draw = ImageDraw.Draw(canvas)
    font = ImageFont.load_default()

    for i, ((img, total, lo, hi), label) in enumerate(zip(panels, labels)):
        x = PADDING + i * (panel_w + PADDING)
        canvas.paste(img, (x, LABEL_HEIGHT))
        draw.text((x, 2), f"{label}  sum={total}  [{lo}-{hi}]", fill="black", font=font)

    canvas.save(args.out)
    print(f"wrote {args.out} ({canvas_w}x{canvas_h})")


if __name__ == "__main__":
    main()
