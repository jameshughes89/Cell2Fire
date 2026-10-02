#!/usr/bin/env python3
"""
Structural ablation on Dogrib (stochastic, ROS-CV 0.2), mirroring run_stochastic.py exactly.

Strategies: tuned proximity (proxA_5.0, selected on the wildfireGP validation panel) and the eight structural
variants added to Treatments.cpp (plusdist, nodist, gated, simplified). Seeds 1-30, both min_dists, no "none".
Resumable: jobs already present in the output CSV are skipped.

Output: ABLATION_OUT env var, or ablation_results_stochastic.csv in the repo root.
"""

import csv
import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
os.environ.setdefault("STOCHASTIC_OUT", os.environ.get(
    "ABLATION_OUT", str(Path(__file__).resolve().parent.parent / "ablation_results_stochastic.csv")))
os.environ.setdefault("SEED_START", "1")
os.environ.setdefault("SEED_END", "30")

import run_stochastic as rs  # noqa: E402

rs.ACTIVE_STRATEGIES = [
    "proxA_5.0",
    "lowonly_plusdist", "hilly_plusdist", "barriers_plusdist",
    "oa_nodist", "hb_nodist", "ga_nodist",
    "oa_gated", "oa_simplified",
]


def build_jobs():
    done = set()
    if rs.OUT_CSV.exists():
        for r in csv.DictReader(open(rs.OUT_CSV)):
            if r["returncode"] == "0":
                done.add((r["quadrant"], r["wind_speed"], r["wind_dir"], r["strategy"], r["min_dist"], r["seed"]))
    # Seed-major order: every strategy and scenario completes seeds 1-10 before 11-20, so a complete paired subset
    # is available early in a multi-day run.
    jobs = []
    for seed in rs.SEEDS:
        for quad, cell_id in rs.IGNITIONS.items():
            for wind_speed in rs.WIND_SPEEDS:
                for wind_dir in rs.WIND_DIRS:
                    for strategy in rs.ACTIVE_STRATEGIES:
                        for min_dist in rs.MIN_DISTS:
                            key = (quad, wind_speed, str(wind_dir), strategy, str(min_dist), str(seed))
                            if key not in done:
                                jobs.append((quad, cell_id, wind_speed, wind_dir, strategy, min_dist, seed))
    return jobs


rs.build_jobs = build_jobs

if __name__ == "__main__":
    rs.main()
