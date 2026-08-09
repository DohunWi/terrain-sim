#!/usr/bin/env python3
"""Capture the deterministic integrator sweep with public run metadata."""

from __future__ import annotations

import argparse
import json
import subprocess
import tempfile
from datetime import datetime, timezone
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--commit", required=True)
    args = parser.parse_args()

    with tempfile.TemporaryDirectory(prefix="terrain-integrators-") as temp_dir:
        raw_path = Path(temp_dir) / "raw.json"
        subprocess.run([str(args.binary), str(raw_path)], check=True, capture_output=True, text=True)
        raw = json.loads(raw_path.read_text())

    document = {
        "experiment_id": "physics-integrator-energy-drift",
        "measured_at_utc": datetime.now(timezone.utc).isoformat(),
        "commit_sha": args.commit,
        "scope": "1D harmonic oscillator (k=1, m=1) over 20 periods",
        "operating_dt_seconds": 1.0 / 60.0,
        "compiler": "Apple Clang 17.0.0",
        "compiler_flags": "-std=c++20 -O3 -DNDEBUG -Wall -Wextra",
        "hardware": {
            "model": "MacBook Air",
            "chip": "Apple M2",
            "cpu_cores": "8 (4 performance + 4 efficiency)",
            "memory_gb": 8,
            "architecture": "arm64",
        },
        "operating_system": "macOS 26.5.1 (25F80)",
        "rows": raw["rows"],
        "limitations": [
            "isolated numerical model; it does not include terrain contact",
            "energy drift is deterministic numerical evidence, not a wall-clock performance measurement",
            "force-evaluation count is reported separately from numerical drift",
        ],
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(document, indent=2) + "\n")


if __name__ == "__main__":
    main()
