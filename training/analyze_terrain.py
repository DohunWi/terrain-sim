"""Terrain difficulty analysis per docs/evaluation-protocol.md §7.

Measures whether the generated terrain actually gates start-goal navigation
the way the task is supposed to: per-cell slope angle distribution, fraction
of cells steeper than theta_max = atan(F_MAX / (mass*gravity)), fraction of
straight start-goal lines that cross an unclimbable cell, and fraction of
those that still have a walkable detour (BFS over cells with slope <= theta_max).

Reuses TerrainAgentEnv.reset() directly (not a reimplementation of terrain
generation) so every measured terrain+start+goal triple is exactly what
training/eval would have seen for that seed.
"""

from __future__ import annotations

import argparse
import json
import math
import pathlib
from collections import deque

import numpy as np

from env import TerrainAgentEnv
from repo_meta import env_constants, git_commit_sha, git_is_dirty


def slope_angle_grid(heightmap) -> np.ndarray:
    w, h = heightmap.width, heightmap.height
    angles = np.empty((h, w), dtype=np.float32)
    for z in range(h):
        for x in range(w):
            s = heightmap.sample(float(x), float(z))
            slope = math.hypot(s.grad_x, s.grad_y)
            angles[z, x] = math.degrees(math.atan(slope))
    return angles


def straight_line_blocked(heightmap, sx, sz, gx, gz, theta_max_deg, n_samples=200) -> bool:
    for i in range(n_samples + 1):
        t = i / n_samples
        x, z = sx + (gx - sx) * t, sz + (gz - sz) * t
        s = heightmap.sample(x, z)
        angle = math.degrees(math.atan(math.hypot(s.grad_x, s.grad_y)))
        if angle > theta_max_deg:
            return True
    return False


def detour_exists(angle_grid: np.ndarray, sx, sz, gx, gz, theta_max_deg) -> bool:
    h, w = angle_grid.shape
    passable = angle_grid <= theta_max_deg

    start_cell = (int(round(sz)) % h, int(round(sx)) % w)
    goal_cell = (int(round(gz)) % h, int(round(gx)) % w)
    passable[start_cell] = True
    passable[goal_cell] = True

    visited = np.zeros_like(passable, dtype=bool)
    visited[start_cell] = True
    queue = deque([start_cell])
    neighbors = [(-1, -1), (-1, 0), (-1, 1), (0, -1), (0, 1), (1, -1), (1, 0), (1, 1)]

    while queue:
        cz, cx = queue.popleft()
        if (cz, cx) == goal_cell:
            return True
        for dz, dx in neighbors:
            nz, nx = cz + dz, cx + dx
            if 0 <= nz < h and 0 <= nx < w and not visited[nz, nx] and passable[nz, nx]:
                visited[nz, nx] = True
                queue.append((nz, nx))
    return visited[goal_cell]


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--n-terrains", type=int, default=100)
    parser.add_argument("--seed-start", type=int, default=0)
    parser.add_argument("--out", type=pathlib.Path,
                         default=pathlib.Path("../benchmarks/evaluations/terrain_difficulty.json"))
    parser.add_argument("--scale", type=float, default=None)
    parser.add_argument("--octaves", type=int, default=None)
    parser.add_argument("--persistence", type=float, default=None)
    parser.add_argument("--lacunarity", type=float, default=None)
    parser.add_argument("--talus-angle", type=float, default=None)
    parser.add_argument("--erosion-rate", type=float, default=None)
    parser.add_argument("--erosion-iterations", type=int, default=None)
    parser.add_argument("--f-max", type=float, default=None)
    args = parser.parse_args()

    env = TerrainAgentEnv(
        scale=args.scale, octaves=args.octaves, persistence=args.persistence,
        lacunarity=args.lacunarity, talus_angle=args.talus_angle,
        erosion_rate=args.erosion_rate, erosion_iterations=args.erosion_iterations,
        f_max=args.f_max,
    )
    gravity = abs(env._gravity.y)
    theta_max_deg = math.degrees(math.atan(env.F_MAX / (env.MASS * gravity)))
    print(f"F_MAX={env.F_MAX}, mass={env.MASS}, gravity={gravity} -> theta_max={theta_max_deg:.2f} deg")

    all_angles = []
    n_blocked = 0
    n_detour_exists_given_blocked = 0
    n_reachable_at_all = 0

    for i in range(args.n_terrains):
        seed = args.seed_start + i
        env.reset(seed=seed)
        hm = env._heightmap
        sx, sz = env._body.position.x, env._body.position.z
        gx, gz = env._goal_x, env._goal_z

        angle_grid = slope_angle_grid(hm)
        all_angles.append(angle_grid.flatten())

        blocked = straight_line_blocked(hm, sx, sz, gx, gz, theta_max_deg)
        reachable = detour_exists(angle_grid, sx, sz, gx, gz, theta_max_deg)

        if blocked:
            n_blocked += 1
            if reachable:
                n_detour_exists_given_blocked += 1
        if reachable:
            n_reachable_at_all += 1

    angles = np.concatenate(all_angles)
    n = args.n_terrains

    result = {
        "n_terrains": n,
        "seed_range": [args.seed_start, args.seed_start + n - 1],
        "env_constants": env_constants(env),
        "gravity": gravity,
        "theta_max_deg": theta_max_deg,
        "commit_sha": git_commit_sha(),
        "git_dirty": git_is_dirty(),
        "slope_angle_percentiles_deg": {
            "p50": float(np.percentile(angles, 50)),
            "p90": float(np.percentile(angles, 90)),
            "p95": float(np.percentile(angles, 95)),
            "p99": float(np.percentile(angles, 99)),
            "max": float(np.max(angles)),
        },
        "fraction_cells_over_theta_max": float(np.mean(angles > theta_max_deg)),
        "fraction_straight_line_blocked": n_blocked / n,
        "fraction_blocked_with_detour": (n_detour_exists_given_blocked / n_blocked) if n_blocked else None,
        "fraction_reachable_at_all": n_reachable_at_all / n,
    }

    print(json.dumps(result, indent=2))

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, indent=2))
    print(f"\nwrote {args.out}")


if __name__ == "__main__":
    main()
