"""Feature parity between the Cell2Fire treatment hook and wildfireGP.

The evolved policies were trained on wildfireGP's feature definitions (``wildfireGP/features.py``), so the port must
compute the same values from the same fire state. Each test builds a wildfireGP fire state with burning, burned,
treated, water and rock cells, converts it to Cell2Fire's representation (fire state in burningCells/burntCells,
statusCells 5 = treated and 4 = non-fuel, static fuel), runs ``feature_parity/feature_dump.cpp`` on it, and compares
one feature over every treatment candidate.

wildfireGP is imported from ``$WILDFIREGP_PATH`` or a sibling ``wildfireGP`` checkout; run with an interpreter that has
its dependencies, e.g. ``../wildfireGP/venv/bin/python -m pytest tests/test_feature_parity.py``. The harness is
compiled with the local g++ when Boost is available, otherwise in the ``cell2fire:latest`` Docker image.
"""

import functools
import os
import shutil
import subprocess
import sys
from pathlib import Path

import numpy as np
import pytest

REPO_ROOT = Path(__file__).resolve().parent.parent
HARNESS_SRC = REPO_ROOT / "tests" / "feature_parity" / "feature_dump.cpp"
WILDFIREGP_PATH = Path(os.environ.get("WILDFIREGP_PATH", REPO_ROOT.parent / "wildfireGP"))

sys.path.insert(0, str(WILDFIREGP_PATH))
wfgp_features = pytest.importorskip("wildfireGP.features")
from wildfireGP.evaluate import init_ignition  # noqa: E402
from wildfireGP.network import NodeState, TerrainType, create_grid  # noqa: E402
from wildfireGP.spread import spread_step  # noqa: E402

FEATURES = [
    "fuel_level",
    "elevation",
    "slope",
    "distance_to_fire",
    "burnable_distance_to_fire",
    "wind_fire_alignment",
    "has_treated_neighbour",
    "unburnable_neighbour_count",
    "mean_neighbour_fuel",
    "mean_neighbour_elevation",
    "burning_neighbour_count",
    "treated_neighbour_count",
    "unburned_neighbour_count",
    "elevation_delta_to_fire",
]

# Features that depend on which burning cell is nearest. Until the nearest fire is found by the same BFS as
# wildfireGP, ties between equidistant burning cells may resolve differently, so these are compared only on cells
# with a unique nearest burning cell.
NEAREST_FIRE_FEATURES = {"wind_fire_alignment", "elevation_delta_to_fire"}

# (seed, water_fraction, rock_fraction, wind_direction, wind_speed, steps)
STATES = [
    (1, 0.0, 0.0, 0, 10.0, 6),
    (2, 0.15, 0.10, 90, 25.0, 10),
    (3, 0.10, 0.05, 225, 40.0, 14),
    (4, 0.0, 0.10, 315, 5.0, 18),
    (5, 0.20, 0.0, 180, 15.0, 12),
]

ROWS = 30
COLS = 30


@functools.lru_cache(maxsize=1)
def harness_binary() -> Path:
    out_dir = REPO_ROOT / "tests" / "feature_parity" / "build"
    out_dir.mkdir(exist_ok=True)
    binary = out_dir / "feature_dump"
    compile_args = ["-std=c++11", "-O2", "-I/usr/include/eigen3"]
    local = subprocess.run(
        ["g++", *compile_args, str(HARNESS_SRC), "-o", str(binary)], capture_output=True, text=True
    )
    if local.returncode == 0:
        return binary
    if shutil.which("docker") is None:
        pytest.skip("cannot compile harness: no Boost for local g++ and no docker")
    docker = subprocess.run(
        [
            "docker", "run", "--rm",
            "--mount", f"source={REPO_ROOT},destination=/cell2fire,type=bind",
            "-w", "/cell2fire",
            "cell2fire:latest",
            "g++", *compile_args, "tests/feature_parity/feature_dump.cpp", "-o", "tests/feature_parity/build/feature_dump",
        ],
        capture_output=True,
        text=True,
    )
    if docker.returncode != 0:
        pytest.skip(f"cannot compile harness: {docker.stderr.strip()[:500]}")
    return binary


def build_wildfiregp_state(seed: int, water: float, rock: float, wind_dir: int, wind_speed: float, steps: int):
    state = create_grid(ROWS, COLS, water_fraction=water, rock_fraction=rock, seed=seed)
    state.wind_direction = float(wind_dir)
    state.wind_speed = wind_speed
    state.fuel_moisture = 0.2
    pristine_fuel = state.fuel.copy()
    rng = np.random.default_rng(seed)
    land = np.argwhere((state.terrain == TerrainType.LAND) & (state.fuel > 0.0))
    picks = rng.choice(len(land), size=3, replace=False)
    init_ignition(state, [tuple(int(v) for v in land[i]) for i in picks])
    for _ in range(steps):
        candidates = np.argwhere(
            (state.state == NodeState.UNBURNED) & (state.terrain == TerrainType.LAND) & (state.fuel > 0.0)
        )
        for i in rng.choice(len(candidates), size=min(3, len(candidates)), replace=False):
            state.state[tuple(int(v) for v in candidates[i])] = NodeState.TREATED
        spread_step(state, rng)
    wfgp_features.precompute_fire_map(state)
    wfgp_features.precompute_burnable_fire_map(state)
    return state, pristine_fuel


def cell2fire_features(state, pristine_fuel, wind_dir: int) -> np.ndarray:
    status = np.zeros(state.state.shape, dtype=int)
    status[state.state == NodeState.TREATED] = 5
    status[state.terrain != TerrainType.LAND] = 4
    burning = (state.state == NodeState.BURNING).astype(int)
    burned_out = (state.state == NodeState.BURNED).astype(int)
    lines = [f"{ROWS} {COLS} {wind_dir}"]
    for r in range(ROWS):
        for c in range(COLS):
            lines.append(
                f"{status[r, c]} {burning[r, c]} {burned_out[r, c]} {float(pristine_fuel[r, c])!r} "
                f"{float(state.elevation[r, c])!r} {float(state.slope[r, c])!r}"
            )
    result = subprocess.run(
        [str(harness_binary())], input="\n".join(lines) + "\n", capture_output=True, text=True, check=True
    )
    rows = [line.split() for line in result.stdout.strip().split("\n")]
    return np.array([[float(v) for v in row[1:]] for row in rows])


def unique_nearest_fire(state, node: tuple) -> bool:
    burning = np.argwhere(state.state == NodeState.BURNING)
    dist = np.maximum(np.abs(burning[:, 0] - node[0]), np.abs(burning[:, 1] - node[1]))
    return int((dist == dist.min()).sum()) == 1


@functools.lru_cache(maxsize=None)
def comparison(state_index: int) -> dict[str, tuple[np.ndarray, np.ndarray, np.ndarray]]:
    seed, water, rock, wind_dir, wind_speed, steps = STATES[state_index]
    state, pristine_fuel = build_wildfiregp_state(seed, water, rock, wind_dir, wind_speed, steps)
    ported = cell2fire_features(state, pristine_fuel, wind_dir)
    candidates = [
        (int(r), int(c))
        for r, c in np.argwhere(
            (state.state == NodeState.UNBURNED) & (state.terrain == TerrainType.LAND) & (state.fuel > 0.0)
        )
    ]
    unique = np.array([unique_nearest_fire(state, node) for node in candidates])
    out = {}
    for j, name in enumerate(FEATURES):
        expected = np.array([float(getattr(wfgp_features, name)(state, node)) for node in candidates])
        actual = np.array([ported[r * COLS + c, j] for r, c in candidates])
        out[name] = (expected, actual, unique)
    return out


@pytest.mark.parametrize("feature", FEATURES)
@pytest.mark.parametrize("state_index", range(len(STATES)))
def test_feature_matches_wildfiregp(state_index: int, feature: str) -> None:
    expected, actual, unique = comparison(state_index)[feature]
    mask = unique if feature in NEAREST_FIRE_FEATURES else np.ones_like(unique)
    assert np.allclose(actual[mask], expected[mask], rtol=1e-9, atol=1e-9)
