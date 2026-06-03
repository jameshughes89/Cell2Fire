#!/usr/bin/env python3
"""
Full cross-validation matrix with per-seed rows.

4 landscapes × 3 winds × 14 strategies × 30 seeds
+ cell2_ground MinDist=2 × 4 landscapes × 3 winds × 30 seeds
= 5,400 runs total.

All individual rows are kept so distributions can be computed later.
Output path: set FULL_MATRIX_OUT env var or defaults to matrix_results_full.csv
             in the repo root.
"""

import csv
import os
import re
import shutil
import subprocess
import tempfile
from concurrent.futures import ProcessPoolExecutor, as_completed
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
BINARY    = REPO_ROOT / "cell2fire" / "Cell2FireC" / "Cell2Fire"
DATA_ROOT = REPO_ROOT / "data"
OUT_CSV   = Path(os.environ.get("FULL_MATRIX_OUT",
               str(REPO_ROOT / "matrix_results_full.csv")))

LANDSCAPES = {
    "dogrib":     {"ignition": 39795},
    "Arrowhead":  {"ignition": 99433},
    "MicaCreek":  {"ignition": 250273},
    "Revelstoke": {"ignition": 142667},
}

WIND_CONDITIONS = {
    "ws10": "Weather_ws10.csv",
    "ws25": "Weather_ws25.csv",
    "ws43": "Weather_extended.csv",
}

STRATEGIES = [
    "none", "random", "proximity", "fuel_elevation", "neighbour_fuel",
    "shielded_ratio", "open_anchor", "fuel_flank", "cell1_baseline",
    "cell2_ground", "cell3_lowonly", "cell4_highonly", "cell5_hilly",
    "cell6_barriers",
]

SEEDS   = list(range(1, 31))
DELAY   = 3
MAX_WORKERS = 12

COMMON_FLAGS = [
    "--ignitions",
    "--sim-years", "1",
    "--nsims", "1",
    "--weather", "rows",
    "--nweathers", "1",
    "--IgnitionRad", "2",
    "--TreatmentBudget", "3",
    "--TreatmentDelay", str(DELAY),
    "--Fire-Period-Length", "30.0",
    "--max-fire-periods", "200",
    "--ROS-CV", "0.0",
    "--no-output",
]

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


def run_one(landscape, wind_label, strategy, seed, min_dist=0):
    data_dir    = DATA_ROOT / landscape
    weather_src = data_dir / WIND_CONDITIONS[wind_label]

    tmpdir = tempfile.mkdtemp(prefix="c2f_")
    try:
        for f in data_dir.iterdir():
            if f.name != "Weather.csv":
                os.symlink(f.resolve(), os.path.join(tmpdir, f.name))
        os.symlink(weather_src.resolve(), os.path.join(tmpdir, "Weather.csv"))
        os.makedirs(os.path.join(tmpdir, "out"))

        cmd = [
            str(BINARY),
            "--input-instance-folder", tmpdir + os.sep,
            "--output-folder",         os.path.join(tmpdir, "out") + os.sep,
            "--TreatmentStrategy",     strategy,
            "--TreatmentMinDist",      str(min_dist),
            "--seed",                  str(seed),
        ] + COMMON_FLAGS

        proc = subprocess.run(cmd, stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT, text=True)
        output = proc.stdout or ""
        cells_burned, burn_pct, cells_treated, treat_pct, peak_burning, fire_periods = \
            parse_output(output)

        return {
            "landscape":     landscape,
            "wind":          wind_label,
            "strategy":      strategy,
            "min_dist":      min_dist,
            "seed":          seed,
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
            "seed":          seed,
            "cells_burned":  -1, "burn_pct": -1.0,
            "cells_treated": -1, "treat_pct": -1.0,
            "peak_burning":  -1, "fire_periods": -1,
            "returncode":    -99, "error": str(exc),
        }
    finally:
        shutil.rmtree(tmpdir, ignore_errors=True)


def build_jobs():
    jobs = []
    for landscape in LANDSCAPES:
        for wind in WIND_CONDITIONS:
            for strategy in STRATEGIES:
                for seed in SEEDS:
                    jobs.append((landscape, wind, strategy, seed, 0))
            # cell2_ground MinDist=2 at every wind/seed
            for seed in SEEDS:
                jobs.append((landscape, wind, "cell2_ground", seed, 2))
    return jobs


FIELDNAMES = [
    "landscape", "wind", "strategy", "min_dist", "seed",
    "cells_burned", "burn_pct", "cells_treated", "treat_pct",
    "peak_burning", "fire_periods", "returncode",
]


def main():
    jobs = build_jobs()
    total = len(jobs)
    print(f"Total runs: {total}   Workers: {MAX_WORKERS}   Seeds: {min(SEEDS)}–{max(SEEDS)}")
    print(f"Output: {OUT_CSV}\n")

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
            futures = {pool.submit(run_one, *job): job for job in jobs}
            for future in as_completed(futures):
                row = future.result()
                writer.writerow(row)
                fh.flush()
                completed += 1
                if row["returncode"] != 0:
                    failed += 1
                tag = f"  FAILED" if row["returncode"] != 0 else ""
                print(
                    f"[{completed:4d}/{total}] "
                    f"{row['landscape']:12s}  {row['wind']:4s}  "
                    f"{row['strategy']:18s}  md={row['min_dist']}  "
                    f"seed={row['seed']:3d}  "
                    f"burned={row['burn_pct']:.1f}%  "
                    f"fp={row['fire_periods']}"
                    f"{tag}",
                    flush=True,
                )

    print(f"\nDone. {completed} runs, {failed} failed.  Results: {OUT_CSV}")


if __name__ == "__main__":
    main()
