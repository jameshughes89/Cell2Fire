#!/usr/bin/env python3
"""
Cross-validation matrix runner.

Runs all combinations of:
  - 14 strategies  x  3 wind conditions  x  4 landscapes
  + cell2_ground with MinDist=2 on all 3 winds x 4 landscapes (12 extra)
= 180 runs

Metrics parsed from C++ stdout:
  cells_burned, burn_pct, cells_treated, treat_pct, peak_burning, fire_periods

Output: results/matrix_results.csv  (flushed after every row)
"""

import csv
import os
import re
import shutil
import subprocess
import sys
import tempfile
from concurrent.futures import ProcessPoolExecutor, as_completed
from pathlib import Path

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------

REPO_ROOT  = Path(__file__).resolve().parent.parent
BINARY     = REPO_ROOT / "cell2fire" / "Cell2FireC" / "Cell2Fire"
DATA_ROOT  = REPO_ROOT / "data"
OUT_CSV    = REPO_ROOT / "results" / "matrix_results.csv"

LANDSCAPES = {
    "dogrib":     {"ignition": 39795, "rows": 357, "cols": 223},
    "Arrowhead":  {"ignition": 99433, "rows": 461, "cols": 576},
    "MicaCreek":  {"ignition": 250273,"rows": 541, "cols": 644},
    "Revelstoke": {"ignition": 142667,"rows": 539, "cols": 726},
}

WIND_CONDITIONS = {
    "ws10": "Weather_ws10.csv",
    "ws25": "Weather_ws25.csv",
    "ws43": "Weather_extended.csv",
}

STRATEGIES = [
    "none",
    "random",
    "proximity",
    "fuel_elevation",
    "neighbour_fuel",
    "shielded_ratio",
    "open_anchor",
    "fuel_flank",
    "cell1_baseline",
    "cell2_ground",
    "cell3_lowonly",
    "cell4_highonly",
    "cell5_hilly",
    "cell6_barriers",
]

# Shared calibration parameters
COMMON_FLAGS = [
    "--ignitions",
    "--sim-years", "1",
    "--nsims", "1",
    "--weather", "rows",
    "--nweathers", "1",
    "--IgnitionRad", "2",
    "--TreatmentBudget", "3",
    "--TreatmentDelay", "3",
    "--Fire-Period-Length", "30.0",
    "--max-fire-periods", "200",
    "--ROS-CV", "0.0",
    "--seed", "123",
    "--no-output",
]

MAX_WORKERS = 8

# ---------------------------------------------------------------------------
# Result parsing
# ---------------------------------------------------------------------------

RE_BURNT   = re.compile(r"Total Burnt Cells:\s+([\d.]+)\s+-\s+%[^:]+:\s+([\d.]+)%")
RE_TREATED = re.compile(r"Total Treated Cells:\s+([\d.]+)\s+-\s+%[^:]+:\s+([\d.]+)%")
RE_PEAK    = re.compile(r"PeakBurning:\s+(\d+)")
RE_FP      = re.compile(r"FirePeriods:\s+(\d+)")


def parse_output(text):
    m = RE_BURNT.search(text)
    cells_burned = int(float(m.group(1))) if m else -1
    burn_pct     = float(m.group(2))       if m else -1.0

    m = RE_TREATED.search(text)
    cells_treated = int(float(m.group(1))) if m else -1
    treat_pct     = float(m.group(2))       if m else -1.0

    m = RE_PEAK.search(text)
    peak_burning  = int(m.group(1)) if m else -1

    m = RE_FP.search(text)
    fire_periods  = int(m.group(1)) if m else -1

    return cells_burned, burn_pct, cells_treated, treat_pct, peak_burning, fire_periods


# ---------------------------------------------------------------------------
# Single run
# ---------------------------------------------------------------------------

def run_one(landscape, wind_label, strategy, min_dist=0):
    land_info   = LANDSCAPES[landscape]
    weather_src = DATA_ROOT / landscape / WIND_CONDITIONS[wind_label]
    data_dir    = DATA_ROOT / landscape

    tmpdir = tempfile.mkdtemp(prefix="c2f_")
    try:
        # Symlink all data files; Weather.csv points to the chosen wind file.
        for f in data_dir.iterdir():
            if f.name != "Weather.csv":
                os.symlink(f, os.path.join(tmpdir, f.name))
        os.symlink(weather_src, os.path.join(tmpdir, "Weather.csv"))

        out_dir = os.path.join(tmpdir, "out")
        os.makedirs(out_dir)

        cmd = [
            str(BINARY),
            "--input-instance-folder", tmpdir + os.sep,
            "--output-folder",         out_dir + os.sep,
            "--TreatmentStrategy",     strategy,
            "--TreatmentMinDist",      str(min_dist),
        ] + COMMON_FLAGS

        proc = subprocess.run(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
        )
        output = proc.stdout or ""

        cells_burned, burn_pct, cells_treated, treat_pct, peak_burning, fire_periods = \
            parse_output(output)

        return {
            "landscape":     landscape,
            "wind":          wind_label,
            "strategy":      strategy,
            "min_dist":      min_dist,
            "cells_burned":  cells_burned,
            "burn_pct":      burn_pct,
            "cells_treated": cells_treated,
            "treat_pct":     treat_pct,
            "peak_burning":  peak_burning,
            "fire_periods":  fire_periods,
            "returncode":    proc.returncode,
        }

    except Exception as exc:
        return {
            "landscape":     landscape,
            "wind":          wind_label,
            "strategy":      strategy,
            "min_dist":      min_dist,
            "cells_burned":  -1,
            "burn_pct":      -1.0,
            "cells_treated": -1,
            "treat_pct":     -1.0,
            "peak_burning":  -1,
            "fire_periods":  -1,
            "returncode":    -99,
            "error":         str(exc),
        }
    finally:
        shutil.rmtree(tmpdir, ignore_errors=True)


# ---------------------------------------------------------------------------
# Job list construction
# ---------------------------------------------------------------------------

def build_jobs():
    jobs = []
    for landscape in LANDSCAPES:
        for wind in WIND_CONDITIONS:
            for strategy in STRATEGIES:
                jobs.append((landscape, wind, strategy, 0))
            # MinDist=2 extra runs for cell2_ground
            jobs.append((landscape, wind, "cell2_ground", 2))
    return jobs


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

FIELDNAMES = [
    "landscape", "wind", "strategy", "min_dist",
    "cells_burned", "burn_pct", "cells_treated", "treat_pct",
    "peak_burning", "fire_periods", "returncode",
]


def main():
    jobs = build_jobs()
    total = len(jobs)
    print(f"Total runs: {total}  workers: {MAX_WORKERS}")

    OUT_CSV.parent.mkdir(parents=True, exist_ok=True)
    write_header = not OUT_CSV.exists()

    completed = 0
    failed    = 0

    with open(OUT_CSV, "a", newline="") as fh:
        writer = csv.DictWriter(fh, fieldnames=FIELDNAMES, extrasaction="ignore")
        if write_header:
            writer.writeheader()
            fh.flush()

        with ProcessPoolExecutor(max_workers=MAX_WORKERS) as pool:
            futures = {
                pool.submit(run_one, *job): job
                for job in jobs
            }
            for future in as_completed(futures):
                row = future.result()
                writer.writerow(row)
                fh.flush()
                completed += 1
                if row["returncode"] != 0:
                    failed += 1
                    tag = f"  FAILED(rc={row['returncode']})"
                else:
                    tag = ""
                print(
                    f"[{completed:3d}/{total}] "
                    f"{row['landscape']:12s}  {row['wind']:4s}  "
                    f"{row['strategy']:18s}  md={row['min_dist']}  "
                    f"burned={row['burn_pct']:.1f}%  "
                    f"treated={row['cells_treated']}  "
                    f"peak={row['peak_burning']}  "
                    f"fp={row['fire_periods']}"
                    f"{tag}",
                    flush=True,
                )

    print(f"\nDone. {completed} runs, {failed} failed. Results: {OUT_CSV}")


if __name__ == "__main__":
    main()
