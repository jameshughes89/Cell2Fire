import re
import subprocess
from pathlib import Path


def _repo_root() -> Path:
    return Path(__file__).resolve().parents[1]


def _binary_path() -> Path:
    return _repo_root() / "cell2fire" / "Cell2FireC" / "Cell2Fire"


def _run_case(output_folder: Path, spread_radius: int | None) -> str:
    binary = _binary_path()
    assert binary.exists(), f"Cell2Fire binary not found at {binary}"

    cmd = [
        str(binary),
        "--input-instance-folder",
        str(_repo_root() / "data" / "dogrib") + "/",
        "--output-folder",
        str(output_folder),
        "--weather",
        "rows",
        "--nweathers",
        "1",
        "--sim-years",
        "1",
        "--nsims",
        "1",
        "--Fire-Period-Length",
        "1.0",
        "--max-fire-periods",
        "60",
        "--seed",
        "123",
        "--ignitions",
        "--no-output",
    ]
    if spread_radius is not None:
        cmd.extend(["--SpreadRad", str(spread_radius)])

    completed = subprocess.run(cmd, capture_output=True, text=True, check=True)
    return completed.stdout


def _parse_burned_count(stdout: str) -> int:
    match = re.search(r"Total Burnt Cells:\s+(\d+)", stdout)
    assert match is not None, f"Could not parse burnt cells from output:\n{stdout}"
    return int(match.group(1))


def test_spreadrad_default_matches_spreadrad_1(tmp_path: Path) -> None:
    default_out = _run_case(tmp_path / "default", None)
    spread_1_out = _run_case(tmp_path / "spread_1", 1)

    assert _parse_burned_count(default_out) == _parse_burned_count(spread_1_out)
    assert "SpreadRadius: 1" in spread_1_out


def test_spreadrad_2_increases_burned_cells_on_regression_case(tmp_path: Path) -> None:
    spread_1_out = _run_case(tmp_path / "spread_1", 1)
    spread_2_out = _run_case(tmp_path / "spread_2", 2)

    burned_1 = _parse_burned_count(spread_1_out)
    burned_2 = _parse_burned_count(spread_2_out)

    assert "SpreadRadius: 2" in spread_2_out
    assert burned_2 > burned_1, f"Expected SpreadRad=2 to burn more than SpreadRad=1, got {burned_2} <= {burned_1}"
