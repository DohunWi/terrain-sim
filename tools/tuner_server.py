#!/usr/bin/env python3
"""Local live-tuning server for terrain-sim erosion parameters.

Serves tools/tuner.html, which renders sliders built from a per-simulation
parameter manifest, and re-runs the compiled core/build/tune_cli binary on
every slider change to show the real before/after result. Adding a new
simulation (e.g. Phase 2 physics) only needs a manifest entry below plus a
matching --sim= branch in core/src/tune_cli.cpp -- the HTML/JS stays generic.

Usage:
    python3 tools/tuner_server.py [--port 8765]
    (then open http://localhost:8765/)

Requires core/build/tune_cli to already be built (cmake --build core/build).
"""

import argparse
import base64
import io
import json
import subprocess
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlparse, parse_qs

from PIL import Image

from visualize_pgm import read_pgm_p2

REPO_ROOT = Path(__file__).resolve().parent.parent
TUNE_CLI = REPO_ROOT / "core" / "build" / "tune_cli"
HTML_PATH = Path(__file__).resolve().parent / "tuner.html"
SCRATCH_DIR = Path(__file__).resolve().parent / ".tuner_output"

# name/label/default/min/max/step -- shared across every --sim= mode (terrain
# generation happens before the sim-specific branch in tune_cli.cpp).
SHARED_PARAMS = [
    {"name": "width", "label": "Width", "default": 64, "min": 16, "max": 128, "step": 8},
    {"name": "height", "label": "Height", "default": 64, "min": 16, "max": 128, "step": 8},
    {"name": "terrainSeed", "label": "Terrain seed", "default": 42, "min": 0, "max": 1000, "step": 1},
    {"name": "scale", "label": "Noise scale", "default": 10.0, "min": 1, "max": 50, "step": 0.5},
    {"name": "octaves", "label": "Octaves", "default": 3, "min": 1, "max": 8, "step": 1},
    {"name": "persistence", "label": "Persistence", "default": 0.5, "min": 0, "max": 1, "step": 0.01},
    {"name": "lacunarity", "label": "Lacunarity", "default": 2.0, "min": 1, "max": 4, "step": 0.1},
]

SIM_PARAMS = {
    "droplet": [
        {"name": "numDroplets", "label": "Num droplets", "default": 700, "min": 100, "max": 8000, "step": 100},
        {"name": "inertia", "label": "Inertia", "default": 0.3, "min": 0, "max": 1, "step": 0.01},
        {"name": "minSlope", "label": "Min slope", "default": 0.01, "min": 0, "max": 0.2, "step": 0.001},
        {"name": "capacityFactor", "label": "Capacity factor", "default": 4.0, "min": 0.1, "max": 10, "step": 0.1},
        {"name": "erosionFactor", "label": "Erosion factor", "default": 0.3, "min": 0, "max": 1, "step": 0.01},
        {"name": "depositFactor", "label": "Deposit factor", "default": 0.3, "min": 0, "max": 1, "step": 0.01},
        {"name": "gravity", "label": "Gravity", "default": 4.0, "min": 0, "max": 20, "step": 0.5},
        {"name": "evaporateRate", "label": "Evaporate rate", "default": 0.02, "min": 0, "max": 0.5, "step": 0.01},
        {"name": "waterThreshold", "label": "Water threshold", "default": 0.01, "min": 0, "max": 0.5, "step": 0.01},
        {"name": "maxLifeTime", "label": "Max lifetime (steps)", "default": 25, "min": 1, "max": 200, "step": 1},
    ],
    "thermal": [
        {"name": "talusAngle", "label": "Talus angle", "default": 0.1, "min": 0, "max": 2, "step": 0.01},
        {"name": "erosionRate", "label": "Erosion rate", "default": 0.3, "min": 0, "max": 1, "step": 0.01},
        {"name": "iterations", "label": "Iterations", "default": 10, "min": 1, "max": 100, "step": 1},
    ],
}


def pgm_to_data_uri(path: Path, scale: int = 6) -> str:
    width, height, pixels = read_pgm_p2(path)
    img = Image.new("L", (width, height))
    img.putdata(pixels)
    img = img.resize((width * scale, height * scale), Image.NEAREST)
    buf = io.BytesIO()
    img.save(buf, format="PNG")
    return "data:image/png;base64," + base64.b64encode(buf.getvalue()).decode("ascii")


def parse_masscheck(stdout: str) -> dict:
    line = next((l for l in stdout.splitlines() if l.startswith("MASSCHECK")), "")
    stats = {}
    for token in line.replace("MASSCHECK", "").split():
        key, _, val = token.partition("=")
        if key:
            stats[key] = float(val)
    stats["ok"] = abs(stats.get("relDiff", 1.0)) < 1e-4
    return stats


class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        pass  # keep stdout quiet; slider drags fire a lot of requests

    def _send_json(self, payload: dict, status: int = 200):
        body = json.dumps(payload).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        parsed = urlparse(self.path)

        if parsed.path == "/":
            body = HTML_PATH.read_bytes()
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return

        if parsed.path == "/manifest":
            self._send_json({"shared": SHARED_PARAMS, "sims": SIM_PARAMS})
            return

        if parsed.path == "/render":
            query = {k: v[0] for k, v in parse_qs(parsed.query).items()}
            sim = query.get("sim", "droplet")
            if sim not in SIM_PARAMS:
                self._send_json({"error": f"unknown sim '{sim}'"}, status=400)
                return

            SCRATCH_DIR.mkdir(exist_ok=True)
            before_path = SCRATCH_DIR / "before.pgm"
            after_path = SCRATCH_DIR / "after.pgm"

            argv = [str(TUNE_CLI), f"--sim={sim}",
                     f"--outBefore={before_path}", f"--outAfter={after_path}"]
            for spec in SHARED_PARAMS + SIM_PARAMS[sim]:
                name = spec["name"]
                if name in query:
                    argv.append(f"--{name}={query[name]}")

            try:
                result = subprocess.run(argv, capture_output=True, text=True, timeout=30)
            except subprocess.TimeoutExpired:
                self._send_json({"error": "tune_cli timed out"}, status=500)
                return

            if result.returncode != 0:
                self._send_json({"error": result.stderr or "tune_cli failed"}, status=500)
                return

            self._send_json({
                "before": pgm_to_data_uri(before_path),
                "after": pgm_to_data_uri(after_path),
                "stats": parse_masscheck(result.stdout),
            })
            return

        self.send_error(404)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--port", type=int, default=8765)
    args = ap.parse_args()

    if not TUNE_CLI.exists():
        raise SystemExit(f"{TUNE_CLI} not found -- build it first: cmake --build core/build --target tune_cli")

    server = ThreadingHTTPServer(("localhost", args.port), Handler)
    print(f"tuner running at http://localhost:{args.port}/  (Ctrl+C to stop)")
    server.serve_forever()


if __name__ == "__main__":
    main()
