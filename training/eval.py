"""Deterministic policy evaluation per docs/evaluation-protocol.md.

Runs a fixed seed set through a controller (trained PPO policy, or a
direct-to-goal / random baseline -- protocol §5), records the per-episode
fields and aggregate metrics protocol §3/§4 require, and optionally writes
them to a structured JSON under benchmarks/evaluations/. Trajectory+heightmap
dumps for Unity replay (protocol §11: not committed, local-only) stay a
separate opt-in flag.

Usage (run from training/ with the conda Python documented in training/README.md):

    # D20: evaluate one semantic experiment and save a reproducible result.
    /Users/widohun/miniconda3/bin/python3 eval.py \
        --model artifacts/env-maxsteps1000-fmax2/model --controller ppo \
        --episodes 20 --seed-start 1000 --experiment-id env-maxsteps1000-fmax2 \
        --out ../benchmarks/evaluations/env-maxsteps1000-fmax2__train-s0__eval-dev20__controller-ppo.json

    # D20 + Unity replay: dump every episode, then select an OUT_OF_BOUNDS
    # episode from TrajectoryReplay's on-screen list in the Unity Editor.
    /Users/widohun/miniconda3/bin/python3 eval.py \
        --model artifacts/env-maxsteps1000-fmax2/model --controller ppo \
        --episodes 20 --seed-start 1000 --experiment-id env-maxsteps1000-fmax2 \
        --dump-trajectory ../unity-client/Assets/StreamingAssets/env-maxsteps1000-fmax2__eval-dev20__replay.json

--model may omit the .zip suffix. eval.py reads the adjacent meta.json by
default, so the policy is evaluated using its training-time environment constants;
do not pass environment override flags for this like-for-like diagnosis.
"""

from __future__ import annotations

import argparse
import json
import math
import pathlib
import statistics

import numpy as np
from stable_baselines3 import PPO

from env import TerrainAgentEnv
from repo_meta import env_constants, git_commit_sha, git_is_dirty


def slope_deg_from_obs(obs) -> float:
    grad_x, grad_z = float(obs[4]), float(obs[5])
    return math.degrees(math.atan(math.hypot(grad_x, grad_z)))


def select_action(controller: str, obs, model, rng: np.random.Generator | None = None) -> np.ndarray:
    if controller == "ppo":
        action, _ = model.predict(obs, deterministic=True)
        return action
    if controller == "direct":
        # Straight line to the goal, no learning: obs[0:2] is (dx, dz)
        # relative to the goal already (env.py's _observation). A unit
        # vector isn't clipped by the env's L2-norm cap, so this always
        # applies the full F_MAX straight at the goal.
        dx, dz = float(obs[0]), float(obs[1])
        norm = math.hypot(dx, dz)
        if norm < 1e-6:
            return np.zeros(2, dtype=np.float32)
        return np.array([dx / norm, dz / norm], dtype=np.float32)
    if controller == "random":
        # Seeded per-episode (not global np.random) so the "random" baseline
        # is reproducible too -- same seed set, same result, every run.
        return rng.uniform(-1.0, 1.0, size=2).astype(np.float32)
    raise ValueError(f"unknown controller: {controller}")


def classify_outcome(env: TerrainAgentEnv, dist: float) -> str:
    if dist < env.GOAL_RADIUS:
        return "REACHED"
    if env._out_of_bounds():
        return "OUT_OF_BOUNDS"
    return "TIMED_OUT"


def run_episode(env: TerrainAgentEnv, model, controller: str, seed: int, dump: bool):
    obs, _ = env.reset(seed=seed)
    start_x, start_z = env._body.position.x, env._body.position.z
    rng = np.random.default_rng(seed)

    slopes = [slope_deg_from_obs(obs)]
    trajectory = []
    ep_return = 0.0
    terminated = truncated = False

    while not (terminated or truncated):
        if dump:
            p = env._body.position
            trajectory.append({"x": p.x, "y": p.y, "z": p.z})
        action = select_action(controller, obs, model, rng)
        obs, reward, terminated, truncated, _ = env.step(action)
        ep_return += reward
        slopes.append(slope_deg_from_obs(obs))

    dist = env._distance_to_goal()
    outcome = classify_outcome(env, dist)

    record = {
        "seed": seed,
        "outcome": outcome,
        "episode_steps": env._step_count,
        "return": ep_return,
        "final_distance": dist,
        "start_x": start_x,
        "start_z": start_z,
        "goal_x": env._goal_x,
        "goal_z": env._goal_z,
        "max_slope_deg": max(slopes),
        "mean_slope_deg": sum(slopes) / len(slopes),
    }

    dump_record = None
    if dump:
        dump_record = {
            "index": seed,
            "outcome": outcome,
            "steps": env._step_count,
            "finalDist": dist,
            "width": env._heightmap.width,
            "height": env._heightmap.height,
            "heightmap": env._heightmap.to_numpy().flatten().tolist(),
            "goalX": env._goal_x,
            "goalZ": env._goal_z,
            "trajectory": trajectory,
        }

    return record, dump_record


def summarize(records: list[dict]) -> dict:
    n = len(records)
    outcomes = [r["outcome"] for r in records]
    lengths = [r["episode_steps"] for r in records]
    reached_lengths = [r["episode_steps"] for r in records if r["outcome"] == "REACHED"]

    def rate(name: str) -> float:
        return outcomes.count(name) / n

    def mean_final_dist(name: str) -> float | None:
        vals = [r["final_distance"] for r in records if r["outcome"] == name]
        return sum(vals) / len(vals) if vals else None

    return {
        "n_episodes": n,
        "success_count": outcomes.count("REACHED"),
        "success_rate": rate("REACHED"),
        "out_of_bounds_count": outcomes.count("OUT_OF_BOUNDS"),
        "out_of_bounds_rate": rate("OUT_OF_BOUNDS"),
        "timeout_count": outcomes.count("TIMED_OUT"),
        "timeout_rate": rate("TIMED_OUT"),
        "mean_episode_length": sum(lengths) / n,
        "median_episode_length": statistics.median(lengths),
        "mean_success_episode_length": sum(reached_lengths) / len(reached_lengths) if reached_lengths else None,
        "median_success_episode_length": statistics.median(reached_lengths) if reached_lengths else None,
        "mean_final_distance_by_outcome": {
            "REACHED": mean_final_dist("REACHED"),
            "OUT_OF_BOUNDS": mean_final_dist("OUT_OF_BOUNDS"),
            "TIMED_OUT": mean_final_dist("TIMED_OUT"),
        },
        "mean_return": sum(r["return"] for r in records) / n,
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--model", type=pathlib.Path,
                        default=pathlib.Path("artifacts/baseline-l2cap-fmax5/model"))
    parser.add_argument("--controller", choices=["ppo", "direct", "random"], default="ppo")
    parser.add_argument("--episodes", type=int, default=20)
    parser.add_argument("--seed-start", type=int, default=1000)
    parser.add_argument("--dump-trajectory", type=pathlib.Path, default=None,
                         help="also write every episode's terrain+trajectory (Unity replay JSON)")
    parser.add_argument("--experiment-id", type=str, required=True,
                         help="semantic condition ID stored in the output JSON, "
                              "e.g. baseline-l2cap-fmax5")
    parser.add_argument("--out", type=pathlib.Path, default=None,
                         help="write structured results to this path under benchmarks/evaluations/")
    # Env overrides: default to the model's own training config (meta.json,
    # written by train.py) so eval never silently drifts from what the policy
    # was trained under. Pass any of these explicitly only for an intentional
    # out-of-distribution test (e.g. --map-size for a bigger map).
    parser.add_argument("--map-size", type=int, default=None)
    parser.add_argument("--scale", type=float, default=None)
    parser.add_argument("--octaves", type=int, default=None)
    parser.add_argument("--persistence", type=float, default=None)
    parser.add_argument("--lacunarity", type=float, default=None)
    parser.add_argument("--talus-angle", type=float, default=None)
    parser.add_argument("--erosion-rate", type=float, default=None)
    parser.add_argument("--erosion-iterations", type=int, default=None)
    parser.add_argument("--f-max", type=float, default=None)
    parser.add_argument("--max-steps", type=int, default=None)
    args = parser.parse_args()

    meta_path = args.model.parent / "meta.json"
    training_meta = json.loads(meta_path.read_text()) if meta_path.exists() else None
    if training_meta is None:
        print(f"WARNING: no {meta_path} -- evaluating with class-default env constants, "
              f"not necessarily what this model was trained under.")

    trained_env_constants = training_meta["env_constants"] if training_meta else {}

    def resolved(cli_value, key):
        return cli_value if cli_value is not None else trained_env_constants.get(key)

    model = PPO.load(str(args.model)) if args.controller == "ppo" else None
    env = TerrainAgentEnv(
        map_size=resolved(args.map_size, "MAP_SIZE"),
        scale=resolved(args.scale, "SCALE"),
        octaves=resolved(args.octaves, "OCTAVES"),
        persistence=resolved(args.persistence, "PERSISTENCE"),
        lacunarity=resolved(args.lacunarity, "LACUNARITY"),
        talus_angle=resolved(args.talus_angle, "TALUS_ANGLE"),
        erosion_rate=resolved(args.erosion_rate, "EROSION_RATE"),
        erosion_iterations=resolved(args.erosion_iterations, "EROSION_ITERATIONS"),
        f_max=resolved(args.f_max, "F_MAX"),
        max_steps=resolved(args.max_steps, "MAX_STEPS"),
    )

    records = []
    dump_records = []
    for i in range(args.episodes):
        seed = args.seed_start + i
        record, dump_record = run_episode(env, model, args.controller, seed, dump=bool(args.dump_trajectory))
        records.append(record)
        if dump_record is not None:
            dump_records.append(dump_record)
        print(f"seed {seed:5d}: {record['outcome']:14s} steps={record['episode_steps']:4d} "
              f"return={record['return']:7.2f} final_dist={record['final_distance']:6.2f} "
              f"max_slope={record['max_slope_deg']:5.1f}deg")

    summary = summarize(records)
    print(f"\n{summary['success_count']}/{summary['n_episodes']} reached "
          f"({summary['success_rate']:.0%}), "
          f"{summary['out_of_bounds_count']}/{summary['n_episodes']} out of bounds "
          f"({summary['out_of_bounds_rate']:.0%}), "
          f"{summary['timeout_count']}/{summary['n_episodes']} timed out "
          f"({summary['timeout_rate']:.0%})")
    print(f"mean/median episode length: {summary['mean_episode_length']:.1f} / {summary['median_episode_length']:.1f}")
    if summary["mean_success_episode_length"] is not None:
        print(f"mean/median SUCCESS episode length: "
              f"{summary['mean_success_episode_length']:.1f} / {summary['median_success_episode_length']:.1f}")
    print(f"mean return: {summary['mean_return']:.2f}")

    if args.out is not None:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        payload = {
            "experiment_id": args.experiment_id,
            "model_path": str(args.model.resolve()) if model is not None else None,
            "controller": args.controller,
            "seed_range": [args.seed_start, args.seed_start + args.episodes - 1],
            "eval_env_constants": env_constants(env),
            "training_metadata": training_meta,
            "eval_commit_sha": git_commit_sha(),
            "eval_git_dirty": git_is_dirty(),
            "summary": summary,
            "episodes": records,
        }
        args.out.write_text(json.dumps(payload, indent=2))
        print(f"\nwrote structured results to {args.out}")

    if args.dump_trajectory:
        args.dump_trajectory.parent.mkdir(parents=True, exist_ok=True)
        args.dump_trajectory.write_text(json.dumps({"episodes": dump_records}))
        print(f"wrote {len(dump_records)} episode replays to {args.dump_trajectory}")


if __name__ == "__main__":
    main()
