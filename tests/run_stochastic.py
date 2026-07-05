#!/usr/bin/env python3
"""
Stochastic matrix: dogrib, 5 ignitions, 4 wind dirs, 3 wind speeds,
all 15 strategies × md={0,2} (none md=0 only), ROS-CV=0.2.

Seed range is controlled via env vars SEED_START / SEED_END (inclusive),
defaulting to 1–10. Run a second batch with SEED_START=11 SEED_END=20
and results append cleanly to the same CSV.

Total (seeds 1-10):
  none (md=0 only):  5 × 4 × 3 × 10 =    600
  14 active × md×2:  5 × 4 × 3 × 2 × 14 × 10 = 16,800
  Total: 17,400 runs

Output: set STOCHASTIC_OUT env var or defaults to matrix_results_stochastic.csv
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
DATA_DIR  = REPO_ROOT / "data" / "dogrib"
OUT_CSV   = Path(os.environ.get("STOCHASTIC_OUT",
               str(REPO_ROOT / "matrix_results_stochastic.csv")))

SEED_START = int(os.environ.get("SEED_START", "1"))
SEED_END   = int(os.environ.get("SEED_END",   "10"))
SEEDS      = list(range(SEED_START, SEED_END + 1))

ROS_CV = "0.2"

IGNITIONS = {
    "centre": 39795,
    "NW":     19903,
    "NE":     20015,
    "SW":     59597,
    "SE":     59709,
}

WIND_SPEEDS = {
    "ws10": "Weather_ws10.csv",
    "ws25": "Weather_ws25.csv",
    "ws43": "Weather_extended.csv",
}

WIND_DIRS = [0, 90, 180, 270]

ACTIVE_STRATEGIES = [
    "random", "proximity", "open_anchor",
    "cell1_baseline", "cell2_ground",
    "cell3_lowonly", "cell4_highonly",
    "cell5_hilly", "cell6_barriers",
    "cell7_cranked",
    "fuel_elevation", "neighbour_fuel",
    "shielded_ratio", "fuel_flank",
]
MIN_DISTS = [0, 2]

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
    "--ROS-CV", ROS_CV,
    "--no-output",
]

MAX_WORKERS = 14

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


def make_weather(src_path, wind_dir, tmpdir):
    dst = os.path.join(tmpdir, "Weather.csv")
    with open(src_path) as fin, open(dst, "w", newline="") as fout:
        reader = csv.DictReader(fin)
        writer = csv.DictWriter(fout, fieldnames=reader.fieldnames)
        writer.writeheader()
        for row in reader:
            row["WD"] = str(wind_dir)
            writer.writerow(row)
    return dst


def run_one(quad, cell_id, wind_speed, wind_dir, strategy, min_dist, seed):
    weather_src = str((DATA_DIR / WIND_SPEEDS[wind_speed]).resolve())
    tmpdir = tempfile.mkdtemp(prefix="c2f_")
    try:
        for f in DATA_DIR.iterdir():
            if f.name not in ("Weather.csv", "Ignitions.csv"):
                os.symlink(f.resolve(), os.path.join(tmpdir, f.name))

        make_weather(weather_src, wind_dir, tmpdir)

        with open(os.path.join(tmpdir, "Ignitions.csv"), "w") as f:
            f.write(f"Year,Ncell\n1,{cell_id}\n")

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
            "quadrant":      quad,
            "ignition_cell": cell_id,
            "wind_speed":    wind_speed,
            "wind_dir":      wind_dir,
            "strategy":      strategy,
            "min_dist":      min_dist,
            "seed":          seed,
            "ros_cv":        float(ROS_CV),
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
            "quadrant": quad, "ignition_cell": cell_id,
            "wind_speed": wind_speed, "wind_dir": wind_dir,
            "strategy": strategy, "min_dist": min_dist, "seed": seed,
            "ros_cv": float(ROS_CV),
            "cells_burned": -1, "burn_pct": -1.0,
            "cells_treated": -1, "treat_pct": -1.0,
            "peak_burning": -1, "fire_periods": -1,
            "returncode": -99, "error": str(exc),
        }
    finally:
        shutil.rmtree(tmpdir, ignore_errors=True)


def build_jobs():
    jobs = []
    for quad, cell_id in IGNITIONS.items():
        for wind_speed in WIND_SPEEDS:
            for wind_dir in WIND_DIRS:
                # none — md=0 only
                for seed in SEEDS:
                    jobs.append((quad, cell_id, wind_speed, wind_dir, "none", 0, seed))
                # active strategies — both min_dists
                for strategy in ACTIVE_STRATEGIES:
                    for min_dist in MIN_DISTS:
                        for seed in SEEDS:
                            jobs.append((quad, cell_id, wind_speed, wind_dir,
                                         strategy, min_dist, seed))
    return jobs


FIELDNAMES = [
    "quadrant", "ignition_cell", "wind_speed", "wind_dir",
    "strategy", "min_dist", "seed", "ros_cv",
    "cells_burned", "burn_pct", "cells_treated", "treat_pct",
    "peak_burning", "fire_periods", "returncode",
]


def main():
    jobs = build_jobs()
    total = len(jobs)
    n_variants = 1 + len(ACTIVE_STRATEGIES) * len(MIN_DISTS)
    print(f"Landscape: dogrib   ROS-CV: {ROS_CV}")
    print(f"Seeds: {SEED_START}–{SEED_END}   "
          f"Ignitions: {len(IGNITIONS)}   Wind dirs: {len(WIND_DIRS)}   "
          f"Wind speeds: {len(WIND_SPEEDS)}   Strategy-variants: {n_variants}")
    print(f"Total runs: {total}   Workers: {MAX_WORKERS}")
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
                tag = "  FAILED" if row["returncode"] != 0 else ""
                print(
                    f"[{completed:6d}/{total}]  "
                    f"{row['quadrant']:6s}  wd={row['wind_dir']:3d}  {row['wind_speed']:4s}  "
                    f"{row['strategy']:18s}  md={row['min_dist']}  "
                    f"seed={row['seed']:2d}  "
                    f"burned={row['burn_pct']:.1f}%"
                    f"{tag}",
                    flush=True,
                )

    print(f"\nDone. {completed} runs, {failed} failed.  Results: {OUT_CSV}")


if __name__ == "__main__":
    main()
