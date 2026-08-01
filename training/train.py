"""stable-baselines3 PPO training entry point for TerrainAgentEnv.

Writes <out>.meta.json alongside the saved model with everything
docs/evaluation-protocol.md §1 requires to make a run reproducible/comparable:
training seed, total steps, PPO hyperparameters, env constants, and the repo
commit SHA (+ dirty flag) at training time.

tensorborad /Users/widohun/miniconda3/bin/python3 -m tensorboard.main --logdir artifacts --port 6006
"""

from __future__ import annotations

import argparse
import json
import pathlib

from stable_baselines3 import PPO
from stable_baselines3.common.monitor import Monitor

from env import TerrainAgentEnv
from repo_meta import env_constants, git_commit_sha, git_is_dirty, ppo_hyperparams


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--timesteps", type=int, default=200_000)
    parser.add_argument("--seed", type=int, default=0)
    parser.add_argument("--out", type=pathlib.Path, default=pathlib.Path("ppo_terrain_agent"))
    parser.add_argument("--log-dir", type=pathlib.Path, default=pathlib.Path("logs"))
    parser.add_argument("--tb-log-dir", type=pathlib.Path, default=pathlib.Path("tb_logs"))
    parser.add_argument("--run-name", type=str, default="ppo")
    parser.add_argument("--experiment-id", type=str, default=None)
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

    args.log_dir.mkdir(parents=True, exist_ok=True)
    raw_env = TerrainAgentEnv(
        map_size=args.map_size, scale=args.scale, octaves=args.octaves,
        persistence=args.persistence, lacunarity=args.lacunarity,
        talus_angle=args.talus_angle, erosion_rate=args.erosion_rate,
        erosion_iterations=args.erosion_iterations, f_max=args.f_max,
        max_steps=args.max_steps,
    )
    env = Monitor(raw_env, filename=str(args.log_dir / "monitor"))
    model = PPO("MlpPolicy", env, seed=args.seed, verbose=1, tensorboard_log=str(args.tb_log_dir))
    model.learn(total_timesteps=args.timesteps, tb_log_name=args.run_name)
    model.save(str(args.out))
    print(f"saved to {args.out}.zip")

    meta = {
        "experiment_id": args.experiment_id,
        "training_seed": args.seed,
        "total_timesteps": args.timesteps,
        "ppo_hyperparams": ppo_hyperparams(model),
        "env_constants": env_constants(raw_env),
        "commit_sha": git_commit_sha(),
        "git_dirty": git_is_dirty(),
    }
    meta_path = args.out.parent / "meta.json"
    meta_path.write_text(json.dumps(meta, indent=2))
    print(f"wrote {meta_path}")


if __name__ == "__main__":
    main()
