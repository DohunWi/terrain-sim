"""Gymnasium environment for the Phase 2a minimal agent (single rigid body, no joints).

Task design (observation/action/reward/termination/domain-randomization): see
docs/rl-bindings.md and the terrain-coupling discussion it links to. Terrain-coupling
comes from the physics itself, not a bolted-on penalty: action is a horizontal force
capped at F_MAX (direction-independent, see docs/evaluation-protocol.md §6), and on a
slope steep enough that mass*gravity*tan(theta) > F_MAX the agent physically cannot
climb straight up it -- a purely horizontal push only contributes F*cos(theta) along
the tangent plane, while gravity's tangential pull is mass*gravity*sin(theta)
(core/src/physics/rigid_body.cpp's contact-branch tangential projection already does
this) -- reaching the goal on eroded terrain requires routing around unclimbable
slopes, not just pushing toward the goal in a straight line.
"""

from __future__ import annotations

import math
import pathlib
import sys

import gymnasium as gym
import numpy as np
from gymnasium import spaces

_CORE_BUILD_PYTHON = pathlib.Path(__file__).resolve().parent.parent / "core" / "build" / "python"
if str(_CORE_BUILD_PYTHON) not in sys.path:
    sys.path.insert(0, str(_CORE_BUILD_PYTHON))
import terrain_sim_py as ts  # noqa: E402


class TerrainAgentEnv(gym.Env):
    metadata = {"render_modes": []}

    MAP_SIZE = 64
    SCALE = 10.0
    OCTAVES = 3
    PERSISTENCE = 0.5
    LACUNARITY = 2.0
    # TALUS_ANGLE=0.15 (not the original 0.1) confirmed 2026-08-01 via
    # training/analyze_terrain.py: at F_MAX=5.0 the pre-erosion noise itself
    # never exceeded ~24.8 deg (below theta_max=27.0 deg), so terrain never
    # actually gated navigation regardless of erosion strength -- lowering
    # F_MAX (below) rather than raising terrain roughness was what made the
    # task genuinely terrain-coupled. See docs/evaluation-protocol.md §7/§9
    # and benchmarks/evaluations/terrain_difficulty*.json for the sweep data.
    TALUS_ANGLE = 0.15
    EROSION_RATE = 0.3
    EROSION_ITERATIONS = 10

    MASS = 1.0
    DT = 1.0 / 60.0
    # mass*g = 9.8 -> unclimbable once tan(theta) > F_MAX/9.8, i.e.
    # theta_max = atan(F_MAX / (mass*g)) (NOT asin -- see
    # docs/evaluation-protocol.md §6 for the derivation; action force is
    # horizontal, so only its cos(theta) component lies in the tangent plane).
    # F_MAX=2.0 (theta_max ~= 11.5 deg) confirmed 2026-08-01: blocks ~56% of
    # straight start-goal lines while 100% remain reachable via detour --
    # F_MAX=5.0 (27.0 deg) never blocked anything on this terrain (see the
    # TALUS_ANGLE note above); F_MAX=2.2 (12.7 deg) only blocked 8% (too rare
    # to force detour learning), F_MAX=1.8 (10.4 deg) blocked 91% with some
    # unreachable pairs (too hard).
    F_MAX = 2.0
    # MAX_STEPS=1000 (not the original 500) confirmed 2026-08-01: at F_MAX=2.0
    # even a direct-to-goal controller's successful episodes had a median
    # length of ~376/500 steps on unobstructed terrain, so 500 was starving
    # episodes of time regardless of detour difficulty (55% timeout at
    # MAX_STEPS=500 vs 5% at 1000, same F_MAX/TALUS_ANGLE). Raising the step
    # budget traded that timeout failure mode for a dominant out-of-bounds
    # one instead (20%->75%) -- not yet resolved, see
    # docs/evaluation-protocol.md's next step (E1: add boundary-distance
    # observation).
    MAX_STEPS = 1000
    GOAL_RADIUS = 1.0
    MIN_START_GOAL_DIST = 20.0
    EDGE_MARGIN = 4.0
    STEP_PENALTY = 0.01
    GOAL_REWARD = 50.0
    OUT_OF_BOUNDS_PENALTY = 10.0

    def __init__(
        self,
        map_size: int | None = None,
        scale: float | None = None,
        octaves: int | None = None,
        persistence: float | None = None,
        lacunarity: float | None = None,
        talus_angle: float | None = None,
        erosion_rate: float | None = None,
        erosion_iterations: int | None = None,
        f_max: float | None = None,
        max_steps: int | None = None,
    ):
        super().__init__()
        self.observation_space = spaces.Box(low=-np.inf, high=np.inf, shape=(6,), dtype=np.float32)
        self.action_space = spaces.Box(low=-1.0, high=1.0, shape=(2,), dtype=np.float32)

        # Everything below scales proportionally with map_size relative to the
        # 64 baseline the policy was trained on, EXCEPT SCALE (fbm feature
        # frequency stays per-world-unit so terrain texture/slope statistics
        # don't shift out of the training distribution). Passing a map_size
        # other than the training default is an out-of-distribution
        # generalization test, not something the policy was tuned for.
        if map_size is not None and map_size != self.MAP_SIZE:
            factor = map_size / self.MAP_SIZE
            self.MAP_SIZE = map_size
            self.EDGE_MARGIN = self.EDGE_MARGIN * factor
            self.MIN_START_GOAL_DIST = self.MIN_START_GOAL_DIST * factor
            self.MAX_STEPS = int(self.MAX_STEPS * factor)

        # Direct overrides for terrain-gen/physics tuning sweeps
        # (docs/evaluation-protocol.md) -- default None keeps the class
        # baseline. Changing any of these invalidates a policy trained under
        # different values; retrain before treating results as comparable.
        if scale is not None:
            self.SCALE = scale
        if octaves is not None:
            self.OCTAVES = octaves
        if persistence is not None:
            self.PERSISTENCE = persistence
        if lacunarity is not None:
            self.LACUNARITY = lacunarity
        if talus_angle is not None:
            self.TALUS_ANGLE = talus_angle
        if erosion_rate is not None:
            self.EROSION_RATE = erosion_rate
        if erosion_iterations is not None:
            self.EROSION_ITERATIONS = erosion_iterations
        if f_max is not None:
            self.F_MAX = f_max
        if max_steps is not None:
            self.MAX_STEPS = max_steps  # explicit override wins over the map_size scaling above

        self._gravity = ts.Vec3(0.0, -9.8, 0.0)
        self._heightmap: ts.Heightmap | None = None
        self._body: ts.RigidBody | None = None
        self._goal_x = 0.0
        self._goal_z = 0.0
        self._step_count = 0
        self._prev_dist = 0.0

    def reset(self, *, seed=None, options=None):
        super().reset(seed=seed)
        terrain_seed = int(self.np_random.integers(0, 2**31 - 1))

        hm = ts.generate_fbm_heightmap(
            width=self.MAP_SIZE, height=self.MAP_SIZE, seed=terrain_seed,
            scale=self.SCALE, octaves=self.OCTAVES,
            persistence=self.PERSISTENCE, lacunarity=self.LACUNARITY,
        )
        ts.thermal_erode(hm, talus_angle=self.TALUS_ANGLE, erosion_rate=self.EROSION_RATE,
                          iterations=self.EROSION_ITERATIONS)
        self._heightmap = hm

        lo, hi = self.EDGE_MARGIN, self.MAP_SIZE - self.EDGE_MARGIN
        sx = sz = gx = gz = 0.0
        for _ in range(20):
            sx, sz = self.np_random.uniform(lo, hi, size=2)
            gx, gz = self.np_random.uniform(lo, hi, size=2)
            if math.hypot(gx - sx, gz - sz) >= self.MIN_START_GOAL_DIST:
                break
        self._goal_x, self._goal_z = float(gx), float(gz)

        start_sample = hm.sample(sx, sz)
        body = ts.RigidBody()
        body.position = ts.Vec3(float(sx), start_sample.height, float(sz))
        body.velocity = ts.Vec3(0.0, 0.0, 0.0)
        body.mass = self.MASS
        self._body = body

        self._step_count = 0
        self._prev_dist = self._distance_to_goal()
        return self._observation(), {}

    def step(self, action):
        assert self._body is not None and self._heightmap is not None
        # F_MAX must be a direction-independent force cap (docs/evaluation-protocol.md
        # §6) -- per-axis clip to [-1,1] would let diagonal actions reach
        # F_MAX*sqrt(2), which breaks the theta_max = atan(F_MAX/(m*g)) analysis.
        action = np.asarray(action, dtype=np.float32)
        norm = np.linalg.norm(action)
        if norm > 1.0:
            action = action / norm
        fx, fz = action * self.F_MAX
        force = ts.Vec3(float(fx), 0.0, float(fz))
        ts.step_rigid_body(self._body, self._heightmap, self._gravity, force, self.DT)
        self._step_count += 1

        dist = self._distance_to_goal()
        reward = (self._prev_dist - dist) - self.STEP_PENALTY
        self._prev_dist = dist

        terminated = False
        truncated = False

        if dist < self.GOAL_RADIUS:
            reward += self.GOAL_REWARD
            terminated = True
        elif self._out_of_bounds():
            reward -= self.OUT_OF_BOUNDS_PENALTY
            terminated = True
        elif self._step_count >= self.MAX_STEPS:
            truncated = True

        return self._observation(), reward, terminated, truncated, {}

    def _distance_to_goal(self) -> float:
        p = self._body.position
        return math.hypot(self._goal_x - p.x, self._goal_z - p.z)

    def _out_of_bounds(self) -> bool:
        p = self._body.position
        return not (0.0 <= p.x <= self.MAP_SIZE and 0.0 <= p.z <= self.MAP_SIZE)

    def _observation(self) -> np.ndarray:
        p = self._body.position
        v = self._body.velocity
        # HeightSample.grad_y is gradZ in world space here, same translation
        # rigid_body.cpp does at its call site (see docs/rl-bindings.md).
        s = self._heightmap.sample(p.x, p.z)
        return np.array(
            [self._goal_x - p.x, self._goal_z - p.z, v.x, v.z, s.grad_x, s.grad_y],
            dtype=np.float32,
        )
