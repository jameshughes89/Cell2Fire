#!/usr/bin/env python3
"""
Delay-sensitivity experiment: dogrib, all 14 strategies, 3 wind speeds,
TreatmentDelay in {6, 9}  (delay=3 already in matrix_results.csv).

Output: matrix_results_delay.csv  (includes a 'delay' column; also re-runs
delay=3 for dogrib so all three delays are in one file for easy comparison).
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
OUT_CSV   = Path(os.environ.get("DELAY_OUT",
               str(REPO_ROOT / "matrix_results_delay.csv")))

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

DELAYS = [3, 6, 9]

COMMON_FLAGS = [
    "--ignitions",
    "--sim-years", "1",
    "--nsims", "1",
    "--weather", "rows",
    "--nweathers", "1",
    "--IgnitionRad", "2",
    "--TreatmentBudget", "3",
    "--Fire-Period-Length", "30.0",
    "--max-fire-periods", "200",
    "--ROS-CV", "0.0",
    "--seed", "123",
    "--no-output",
]

MAX_WORKERS = 8

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


def run_one(wind_label, strategy, delay, min_dist=0):
    weather_src = DATA_ROOT / "dogrib" / WIND_CONDITIONS[wind_label]
    data_dir    = DATA_ROOT / "dogrib"

    tmpdir = tempfile.mkdtemp(prefix="c2f_delay_")
    try:
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
            "--TreatmentDelay",        str(delay),
            "--TreatmentMinDist",      str(min_dist),
        ] + COMMON_FLAGS

        proc = subprocess.run(cmd, stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT, text=True)
        output = proc.stdout or ""
        cells_burned, burn_pct, cells_treated, treat_pct, peak_burning, fire_periods = \
            parse_output(output)

        return {
            "wind": wind_label, "strategy": strategy,
            "delay": delay, "min_dist": min_dist,
            "cells_burned": cells_burned, "burn_pct": burn_pct,
            "cells_treated": cells_treated, "treat_pct": treat_pct,
            "peak_burning": peak_burning, "fire_periods": fire_periods,
            "returncode": proc.returncode,
        }
    except Exception as exc:
        return {
            "wind": wind_label, "strategy": strategy,
            "delay": delay, "min_dist": min_dist,
            "cells_burned": -1, "burn_pct": -1.0,
            "cells_treated": -1, "treat_pct": -1.0,
            "peak_burning": -1, "fire_periods": -1,
            "returncode": -99, "error": str(exc),
        }
    finally:
        shutil.rmtree(tmpdir, ignore_errors=True)


def build_jobs():
    jobs = []
    for delay in DELAYS:
        for wind in WIND_CONDITIONS:
            for strategy in STRATEGIES:
                jobs.append((wind, strategy, delay, 0))
            # MinDist=2 extra run for cell2_ground at each wind speed
            jobs.append((wind, "cell2_ground", delay, 2))
    return jobs


FIELDNAMES = [
    "wind", "strategy", "delay", "min_dist",
    "cells_burned", "burn_pct", "cells_treated", "treat_pct",
    "peak_burning", "fire_periods", "returncode",
]


def main():
    jobs = build_jobs()
    total = len(jobs)
    print(f"Landscape: dogrib   Total runs: {total}   Workers: {MAX_WORKERS}")
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
                tag = f"  FAILED(rc={row['returncode']})" if row["returncode"] != 0 else ""
                print(
                    f"[{completed:3d}/{total}]  "
                    f"delay={row['delay']:2d}  {row['wind']:4s}  "
                    f"{row['strategy']:18s}  md={row['min_dist']}  "
                    f"burned={row['burn_pct']:.1f}%  "
                    f"treated={row['cells_treated']}  "
                    f"peak={row['peak_burning']}  "
                    f"fp={row['fire_periods']}"
                    f"{tag}",
                    flush=True,
                )

    print(f"\nDone. {completed} runs, {failed} failed.")


if __name__ == "__main__":
    main()
