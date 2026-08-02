"""Phase 2c performance instrumentation: Python-side timing of the pieces
TerrainAgentEnv (env.py) calls every reset()/step(), plus the pybind11 call
boundary in isolation. Not an RL experiment (no observation/reward/physics
change), so no benchmarks/experiments/ record -- see AGENTS.md's "instrumentation"
row in the change-type table. Compare against core/build/bench's numbers
(core/src/bench.cpp) to see how much of the Python-side cost is the pybind11
crossing itself vs. genuinely different work (RNG start/goal search, numpy
array construction, etc).

Usage:
    /Users/widohun/miniconda3/bin/python3 bench_env.py \
        --out ../benchmarks/evaluations/perf-stack-baseline__bench-python.json
"""

from __future__ import annotations

import argparse
import json
import pathlib
import statistics
import sys
import time

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from env import TerrainAgentEnv  # noqa: E402 (this import adds core/build/python to sys.path)
import terrain_sim_py as ts  # noqa: E402


def time_each(fn, iterations: int, warmup: int) -> list[float]:
    for _ in range(warmup):
        fn()
    samples = []
    for _ in range(iterations):
        start = time.perf_counter()
        fn()
        end = time.perf_counter()
        samples.append((end - start) * 1e6)  # microseconds, matches bench.cpp's units
    return samples


def summarize(samples: list[float]) -> dict:
    return {
        "mean_us": statistics.mean(samples),
        "median_us": statistics.median(samples),
        "min_us": min(samples),
        "max_us": max(samples),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=str, default=None, help="JSON output path")
    parser.add_argument("--reset-iterations", type=int, default=100)
    parser.add_argument("--step-iterations", type=int, default=2000)
    parser.add_argument("--binding-call-iterations", type=int, default=100000)
    args = parser.parse_args()

    results = []

    # 1. Full env.reset() -- terrain regen + thermal erosion + start/goal RNG search.
    env = TerrainAgentEnv()
    reset_samples = time_each(lambda: env.reset(), args.reset_iterations, warmup=5)
    results.append({"name": "env_reset_full", "n": args.reset_iterations, **summarize(reset_samples)})

    # 2. reset()'s C++-bound piece in isolation (generate_fbm_heightmap + thermal_erode
    # only, no Python-side RNG start/goal search) -- isolates how much of reset()'s
    # cost is unavoidable core work vs. env.py's own Python-side logic.
    def terrain_only():
        hm = ts.generate_fbm_heightmap(
            width=env.MAP_SIZE, height=env.MAP_SIZE, seed=0, scale=env.SCALE,
            octaves=env.OCTAVES, persistence=env.PERSISTENCE, lacunarity=env.LACUNARITY,
        )
        ts.thermal_erode(hm, talus_angle=env.TALUS_ANGLE, erosion_rate=env.EROSION_RATE,
                          iterations=env.EROSION_ITERATIONS)

    terrain_samples = time_each(terrain_only, args.reset_iterations, warmup=5)
    results.append({"name": "reset_terrain_gen_only", "n": args.reset_iterations,
                     **summarize(terrain_samples)})

    # 3. Full env.step() -- physics step + reward/termination + observation build.
    env.reset(seed=0)
    action = [0.3, 0.1]
    step_samples = time_each(lambda: env.step(action), args.step_iterations, warmup=50)
    results.append({"name": "env_step_full", "n": args.step_iterations, **summarize(step_samples)})

    # 4. Bare pybind11 call boundary -- ts.step_rigid_body called directly, same
    # args every time, no Python-side reward/obs logic at all. Compare this
    # against core/build/bench's "rigid_body_step" (pure C++, no binding) to
    # estimate the pybind11 crossing overhead itself.
    hm = ts.generate_fbm_heightmap(width=64, height=64, seed=42, scale=10.0, octaves=3,
                                    persistence=0.5, lacunarity=2.0)
    ts.thermal_erode(hm, talus_angle=0.15, erosion_rate=0.3, iterations=10)
    body = ts.RigidBody()
    body.position = ts.Vec3(32.0, hm.sample(32.0, 32.0).height + 5.0, 32.0)
    body.velocity = ts.Vec3(0.0, 0.0, 0.0)
    body.mass = 1.0
    gravity = ts.Vec3(0.0, -9.8, 0.0)
    force = ts.Vec3(0.3, 0.0, 0.1)

    def bare_step_rigid_body():
        ts.step_rigid_body(body, hm, gravity, force, 1.0 / 60.0)
        if not (4.0 < body.position.x < 60.0 and 4.0 < body.position.z < 60.0):
            body.position = ts.Vec3(32.0, hm.sample(32.0, 32.0).height + 5.0, 32.0)
            body.velocity = ts.Vec3(0.0, 0.0, 0.0)

    binding_samples = time_each(bare_step_rigid_body, args.binding_call_iterations, warmup=1000)
    results.append({"name": "step_rigid_body_binding_only", "n": args.binding_call_iterations,
                     **summarize(binding_samples)})

    # 5. Heightmap.sample() through the binding alone (observation path's terrain query).
    def bare_sample():
        hm.sample(32.0, 32.0)

    sample_samples = time_each(bare_sample, args.binding_call_iterations, warmup=1000)
    results.append({"name": "heightmap_sample_binding_only", "n": args.binding_call_iterations,
                     **summarize(sample_samples)})

    print(f"{'stage':<32} {'n':<10} {'mean_us':>10} {'median_us':>10} {'min_us':>10} {'max_us':>10}")
    for r in results:
        print(f"{r['name']:<32} n={r['n']:<8} {r['mean_us']:>10.2f} {r['median_us']:>10.2f} "
              f"{r['min_us']:>10.2f} {r['max_us']:>10.2f}")

    steps_per_sec = 1e6 / summarize(step_samples)["mean_us"]
    print(f"\nimplied single-env throughput: {steps_per_sec:.0f} steps/sec (env.step() only, "
          f"no policy inference)")

    if args.out:
        out_path = pathlib.Path(args.out)
        out_path.parent.mkdir(parents=True, exist_ok=True)
        with out_path.open("w") as f:
            json.dump({"stages": results, "implied_steps_per_sec": steps_per_sec}, f, indent=2)
        print(f"JSON written to {out_path}")


if __name__ == "__main__":
    main()
