#!/usr/bin/env python3
"""
Run new/candidate strategies only against the v2 matrix conditions.

Same protocol as run_v2_matrix.py (dogrib, 5 ignitions, 4 wind dirs,
3 wind speeds, md=0 and md=2, 5 seeds) but limited to NEW_STRATEGIES.
Output appends to a separate CSV so existing v2 results are untouched.

Usage:
    python tests/run_v2_new_strats.py
    V2_NEW_OUT=/path/to/out.csv python tests/run_v2_new_strats.py
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
OUT_CSV   = Path(os.environ.get("V2_NEW_OUT",
               str(REPO_ROOT / "matrix_results_v2_new.csv")))

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

# Add new strategies here as they arrive.
NEW_STRATEGIES = [
    "cell7_cranked",
]
MIN_DISTS = [0, 2]

SEEDS = list(range(1, 6))

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
                for strategy in NEW_STRATEGIES:
                    for min_dist in MIN_DISTS:
                        for seed in SEEDS:
                            jobs.append((quad, cell_id, wind_speed, wind_dir,
                                         strategy, min_dist, seed))
    return jobs


FIELDNAMES = [
    "quadrant", "ignition_cell", "wind_speed", "wind_dir",
    "strategy", "min_dist", "seed",
    "cells_burned", "burn_pct", "cells_treated", "treat_pct",
    "peak_burning", "fire_periods", "returncode",
]


def main():
    jobs = build_jobs()
    total = len(jobs)
    print(f"Landscape: dogrib")
    print(f"New strategies: {NEW_STRATEGIES}")
    print(f"Ignitions: {len(IGNITIONS)}  Wind dirs: {len(WIND_DIRS)}  "
          f"Wind speeds: {len(WIND_SPEEDS)}  Strategy-variants: {len(NEW_STRATEGIES)*len(MIN_DISTS)}  "
          f"Seeds: {len(SEEDS)}")
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
                    f"[{completed:4d}/{total}]  "
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
