# Cell2Fire: A Cell Based Forest Fire Growth Model  C++/Python

**This is a fork of [cell2fire/Cell2Fire](https://github.com/cell2fire/Cell2Fire) by James Hughes.**
The original authors are Cristobal Pais, Jaime Carrasco, David Martell, David L. Woodruff, and Andres Weintraub.

# Disclaimer

This software is for research use only. There is no warranty of any kind;
there is not even the implied warranty of fitness for use.

# This Fork

This fork extends Cell2Fire with **per-timestep reactive treatment allocation**.
The original only supports static pre-treatment plans (harvest masks computed before ignition);
this fork adds a reactive treatment pathway so that an arbitrary cell-scoring expression can
act on the simulation each timestep.

## What the fork adds

1. **A `Treated` cell state.** Cells move into a new firebreak state mid-simulation. Treated
   cells never ignite — incoming fire messages are dropped in `Cell2Fire::GetMessages` and
   defensively in `CellsFBP::get_burned`. Treated is distinct from `Harvested` so initial
   harvest plans and reactive treatments remain separable in output grids
   (Treated → `-2`, Harvested → `-1`).
2. **A per-timestep treatment hook** in the C++ simulation loop that scores all available
   cells and converts the top K to `Treated` before each spread step, with a configurable
   intervention delay, budget, and minimum distance from the fire front.
3. **11 features** computed per candidate cell each timestep:
   - `fuel_level` — Crown Fuel Load from FBP fuel coefficients, normalized [0,1]
   - `elevation` — cell elevation, normalized [0,1]
   - `distance_to_fire` — Chebyshev hops to nearest burning cell (8-connected, unrestricted)
   - `burnable_distance_to_fire` — BFS hops through Available cells only; +inf if cut off by firebreaks
   - `wind_fire_alignment` — cosine of angle between cell→fire and wind-source direction
   - `has_treated_neighbour` — 1.0 if any 8-connected neighbour is Treated
   - `unburnable_neighbour_count` — 8-connected count of Burnt/Harvested/Non-Burnable/Treated neighbours
   - `mean_neighbour_fuel` — mean `fuel_level` of Available 8-neighbours
   - `burning_neighbour_count` — 8-connected count of Burning neighbours
   - `treated_neighbour_count` — 8-connected count of Treated neighbours
   - `unburned_neighbour_count` — 8-connected count of Available neighbours
4. **Seven named strategies** in `cell2fire/Cell2FireC/Treatments.cpp`, selectable at runtime:

   | `--TreatmentStrategy` | Scoring expression |
   |---|---|
   | `none` | No treatments applied |
   | `random` | Uniform random selection |
   | `proximity` | `-burnable_distance_to_fire + 0.1 * has_treated_neighbour` |
   | `fuel_elevation` | `fuel_level + 3*has_treated_neighbour + elevation - burnable_distance_to_fire` |
   | `neighbour_fuel` | `-burnable_distance_to_fire + mean_neighbour_fuel + has_treated_neighbour` |
   | `shielded_ratio` | `(mean_neighbour_fuel - burnable_distance_to_fire - burning_neighbour_count/18 + has_treated_neighbour) / (burnable_distance_to_fire + 2*mean_neighbour_fuel)` |
   | `open_anchor` | `treated_neighbour_count + (mean_neighbour_fuel - 16) / max(0.8, unburned_neighbour_count) * burnable_distance_to_fire` |
   | `fuel_flank` | `mean_neighbour_fuel / max(1, burning_neighbour_count) + has_treated_neighbour - distance_to_fire` |

   To add a new strategy: add a scoring function to `Treatments.cpp`, add an `else if` branch
   in the `computeScore` dispatch, update the comment in `ReadArgs.cpp` and help string in
   `ParseInputs.py`, and recompile.
5. **Intra-step contiguity.** After each of the K placements, the 8 neighbours of the
   just-treated cell are rescored before the next pick, so contiguous barrier segments
   emerge naturally within a single timestep.
6. **Purple Treated rendering** in per-timestep GIF animations. Treated cells appear as
   purple patches overlaid on the forest background.

## Treatment CLI flags

| Flag | Type | Default | Meaning |
|---|---|---|---|
| `--TreatmentBudget` | int | `0` | Cells treated per fire period. `0` disables treatments entirely (fully backward-compatible). |
| `--TreatmentDelay` | int | `0` | Fire periods to skip before the first treatment is applied. |
| `--TreatmentStrategy` | str | `fuel_elevation` | Scoring strategy (see table above). |
| `--TreatmentMinDist` | int | `0` | Minimum BFS hops from the fire front; candidates closer than this are excluded. |

Example (dogrib, proximity strategy, calibrated parameters):
```
python main.py \
  --input-instance-folder ../data/dogrib/ \
  --output-folder ../results/dogrib_proximity \
  --ignitions --sim-years 1 --nsims 1 \
  --weather rows --nweathers 1 \
  --Fire-Period-Length 30.0 --max-fire-periods 200 \
  --IgnitionRad 2 --ROS-CV 0.0 --seed 123 \
  --TreatmentBudget 3 --TreatmentDelay 3 --TreatmentStrategy proximity \
  --grids --allPlots --combine --stats
```

## Status

| Component | Status |
|---|---|
| Build / CI on Python 3.12 (pip, no conda) | done |
| Dockerfile (libgl1, libglib2.0-0, bind-mount workflow) | done |
| `Treated` cell state + receiver-side firebreak guard | done |
| Per-timestep `ApplyTreatments()` hook | done |
| 11-feature extraction per candidate cell | done |
| 8 strategies (none, random, proximity, fuel_elevation, neighbour_fuel, shielded_ratio, open_anchor, fuel_flank) | done |
| Intra-step rescore of treated-cell neighbours | done |
| CLI flags: TreatmentBudget, TreatmentDelay, TreatmentStrategy, TreatmentMinDist | done |
| Purple Treated rendering in GIF animations | done |
| Calibrated test landscapes (dogrib, Arrowhead, MicaCreek, Revelstoke) | done |

See `CLAUDE.md` for the full implementation spec.

# Introduction

Cell2Fire is a cell-based forest and wildland landscape fire spread simulator.
The fire environment is characterized by partitioning the landscape into a large number of homogeneous cells and specifying the fuel, weather, fuel moisture and topography attributes of each cell.
Fire spread within each cell is assumed to be elliptical and governed by spread rates predicted by any independent fire spread model (e.g. the Canadian Forest Fire Behavior Prediction System).
Cell2Fire exploits parallel computation methods which allows users to run large-scale simulations in short periods of time.
It includes powerful statistical, graphical output, and spatial analysis features to facilitate the display and analysis of projected fire growth.

Work in progress documentation is available at
[readthedocs](https://cell2fire.readthedocs.io/) and there is an
original draft of a paper on
[arXiv](https://arxiv.org/abs/1905.09317v1).

# Citation

@ARTICLE{Cell2Fire,
AUTHOR={Pais, Cristobal and Carrasco, Jaime and Martell, David L. and Weintraub, Andres and Woodruff, David L.},
TITLE={Cell2Fire: A Cell-Based Forest Fire Growth Model to Support Strategic Landscape Management Planning},  
JOURNAL={Frontiers in Forests and Global Change},
VOLUME={4},
YEAR={2021},
URL={https://www.frontiersin.org/articles/10.3389/ffgc.2021.692706},
DOI={10.3389/ffgc.2021.692706},
ISSN={2624-893X}
}
   
# Requirements
- g++
- Boost (C++)
- Eigen (C++)
- Python >3.6; you might need 3.12
- numpy
- pandas
- matplotlib
- seaborn
- tqdm
- opencv
- imageio (replaced imread, April 2026)
- networkx (for stats module)

# Installation

Installation may require some familiarity with C++, make, and Python.

System packages (Ubuntu/Debian):
```
sudo apt-get install -y build-essential libboost-all-dev libeigen3-dev
```

Build the C++ simulator and install the Python package into a virtualenv:
```
cd Cell2Fire/cell2fire/Cell2FireC
# (edit Makefile to have the correct path to Eigen, if not /usr/include/eigen3)
make
cd ../..
python -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
pip install -e .
```

Alternatively, a `Dockerfile` is provided that bundles all system and Python
dependencies — see the comment at the top of the file for the build/run
commands.

# Usage
In order to run the simulator (after installation and cd to  Cell2Fire/cell2fire), the following command can be used:
```
$ python main.py --input-instance-folder ../data/Sub40x40/ --output-folder ../results/Sub40x40 --ignitions --sim-years 1 --nsims 5 --finalGrid --weather rows --nweathers 1 --Fire-Period-Length 1.0 --output-messages --ROS-CV 0.0 --seed 123 --stats --allPlots --IgnitionRad 5 --grids --combine
```
For the full list of arguments and their explanation use:
```
$ python main.py -h
```

In addition, both the C++ core and Python scripts can be used separately:
## C++
Only simulation and generate evolution grids (no stats or plots).
```
$ ./Cell2Fire --input-instance-folder ../data/Sub40x40/ --output-folder ../results/Sub40x40 --ignitions --sim-years 1 --nsims 1 --grids --final-grid --Fire-Period-Length 1.0 --weather rows --nweathers 1 --output-messages --ROS-CV 0.0 --seed 123 --IgnitionRad 0 --HFactor 1.0 --FFactor 1.0 --BFactor 1.0 --EFactor 1.0
```

## Python
Only processing option (reads a previously simulated instance and computes stats/plots).
Important: provide the number of sims --nsims to be processed
```
$ python main.py --input-instance-folder ../data/Sub40x40/ --output-folder ../results/Sub40x40_Previous_simulation --nsims 10 --stats --allPlots --onlyProcessing
```

# Output examples
## Dogrib forest (Canadian instance)
![Dogrib Instance](outputs/Example4.png)

## Visualize shortest paths propagation (10 scens)
![Dogrib Fire Propagation and ROS map](outputs/Example1.png)

## Shortest paths propagation and ROS intensity (10 scens)
![Dogrib Fire Propagation map](outputs/Example2.png)

## Burn-Probability maps (10 scens)
![Dogrib BP map](outputs/Example3.png)
