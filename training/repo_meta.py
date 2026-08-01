"""Shared metadata helpers for docs/evaluation-protocol.md §1's record-keeping
requirements (training seed/steps, PPO config, env constants, commit SHA)."""

from __future__ import annotations

import subprocess

# Attributes on TerrainAgentEnv worth recording per experiment -- everything
# that can vary between tuning candidates (protocol §1 "환경 및 물리 상수").
ENV_CONSTANT_NAMES = [
    "MAP_SIZE", "SCALE", "OCTAVES", "PERSISTENCE", "LACUNARITY",
    "TALUS_ANGLE", "EROSION_RATE", "EROSION_ITERATIONS",
    "MASS", "DT", "F_MAX", "MAX_STEPS", "GOAL_RADIUS",
    "MIN_START_GOAL_DIST", "EDGE_MARGIN", "STEP_PENALTY",
    "GOAL_REWARD", "OUT_OF_BOUNDS_PENALTY",
]

PPO_HYPERPARAM_NAMES = [
    "learning_rate", "n_steps", "batch_size", "n_epochs", "gamma",
    "gae_lambda", "clip_range", "ent_coef", "vf_coef", "max_grad_norm", "seed",
]


def env_constants(env) -> dict:
    return {name: getattr(env, name) for name in ENV_CONSTANT_NAMES}


def ppo_hyperparams(model) -> dict:
    result = {}
    for name in PPO_HYPERPARAM_NAMES:
        value = getattr(model, name, None)
        # clip_range is stored as a schedule callable in SB3; evaluate it at
        # progress_remaining=1.0 (start of training) to get the actual number.
        if callable(value):
            try:
                value = value(1.0)
            except TypeError:
                value = str(value)
        result[name] = value
    result["policy_class"] = type(model.policy).__name__
    return result


def git_commit_sha() -> str | None:
    try:
        out = subprocess.run(
            ["git", "rev-parse", "HEAD"], capture_output=True, text=True, check=True,
        )
        return out.stdout.strip()
    except (subprocess.CalledProcessError, FileNotFoundError):
        return None


def git_is_dirty() -> bool | None:
    try:
        out = subprocess.run(
            ["git", "status", "--porcelain"], capture_output=True, text=True, check=True,
        )
        return bool(out.stdout.strip())
    except (subprocess.CalledProcessError, FileNotFoundError):
        return None
