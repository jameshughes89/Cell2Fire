import re
import subprocess
from pathlib import Path


# Ignition cell used for the SpreadRad regression test.  This cell was chosen
# because SpreadRad=1 vs SpreadRad=2 produce a measurable difference within 60
# one-minute periods on the dogrib landscape.  It is kept here so the test does
# not depend on data/dogrib/Ignitions.csv (which is set to our calibrated
# simulation cell and may differ from this value).
_SPREAD_RAD_TEST_CELL = 66850


def _repo_root() -> Path:
    return Path(__file__).resolve().parents[1]


def _binary_path() -> Path:
    return _repo_root() / "cell2fire" / "Cell2FireC" / "Cell2Fire"


def _make_input_dir(base: Path, ignition_cell: int) -> Path:
    """Return a temp input dir that mirrors dogrib but with a custom Ignitions.csv."""
    src = _repo_root() / "data" / "dogrib"
    dst = base / "input"
    dst.mkdir(parents=True)
    for f in src.iterdir():
        if f.name != "Ignitions.csv":
            (dst / f.name).symlink_to(f.resolve())
    (dst / "Ignitions.csv").write_text(f"Year,Ncell\n1,{ignition_cell}\n")
    return dst


def _run_case(input_folder: Path, output_folder: Path, spread_radius: int | None) -> str:
    binary = _binary_path()
    assert binary.exists(), f"Cell2Fire binary not found at {binary}"

    cmd = [
        str(binary),
        "--input-instance-folder",
        str(input_folder) + "/",
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
    input_dir = _make_input_dir(tmp_path / "in_default", _SPREAD_RAD_TEST_CELL)
    default_out = _run_case(input_dir, tmp_path / "default", None)

    input_dir2 = _make_input_dir(tmp_path / "in_spread1", _SPREAD_RAD_TEST_CELL)
    spread_1_out = _run_case(input_dir2, tmp_path / "spread_1", 1)

    assert _parse_burned_count(default_out) == _parse_burned_count(spread_1_out)
    assert "SpreadRadius: 1" in spread_1_out


def test_spreadrad_2_increases_burned_cells_on_regression_case(tmp_path: Path) -> None:
    input_dir1 = _make_input_dir(tmp_path / "in_spread1", _SPREAD_RAD_TEST_CELL)
    spread_1_out = _run_case(input_dir1, tmp_path / "spread_1", 1)

    input_dir2 = _make_input_dir(tmp_path / "in_spread2", _SPREAD_RAD_TEST_CELL)
    spread_2_out = _run_case(input_dir2, tmp_path / "spread_2", 2)

    burned_1 = _parse_burned_count(spread_1_out)
    burned_2 = _parse_burned_count(spread_2_out)

    assert "SpreadRadius: 2" in spread_2_out
    assert burned_2 > burned_1, (
        f"Expected SpreadRad=2 to burn more than SpreadRad=1, got {burned_2} <= {burned_1}"
    )
