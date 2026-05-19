# Cell2Fire: A Cell Based Forest Fire Growth Model  C++/Python
## Cristobal Pais, Jaime Carrasco, David Martell, David L. Woodruff, Andres Weintraub

# Disclaimer

This software is for research use only. There is no warranty of any kind;
there is not even the implied warranty of fitness for use.

![](https://github.com/cell2fire/Cell2Fire/workflows/TestExamples/badge.svg)

# This Fork

This fork extends Cell2Fire with **per-timestep reactive treatment allocation**, providing a
validation environment for scoring-based wildfire suppression strategies on a physically
realistic spread model (Canadian FBP fuel system, elliptical spread, real weather inputs).
The original Cell2Fire only supports static pre-treatment plans (harvest masks computed before
ignition); this fork adds a reactive treatment pathway so that an arbitrary cell-scoring
expression can act on the simulation each timestep.

## What the fork adds

1. **A `Treated` cell state.** Cells move into a new firebreak state mid-simulation. Treated
   cells never ignite — incoming fire messages are dropped both in the receiver path
   (`Cell2Fire::GetMessages`) and defensively in `CellsFBP::get_burned`. Treated is distinct
   from `Harvested` so initial harvest plans and reactive treatments remain separable in
   output grids (Treated → `-2`, Harvested → `-1`).
2. **A per-timestep treatment hook** in the C++ simulation loop that scores all unburned
   burnable cells and converts the top K to `Treated` before each spread step, with a
   configurable intervention delay and budget.
3. **Feature extraction** for the four cell-level primitives: fuel level (Crown Fuel Load from
   the FBP fuel coefficients), Euclidean distance to the nearest burning cell, wind–fire
   alignment (wind vector dotted with the unit vector from the cell toward the nearest burning
   cell), and 4-connected unburnable neighbour count. The unburnable counts and fuel levels
   are precomputed once at construction.
4. **A pluggable C++ scoring function** in `cell2fire/Cell2FireC/Treatments.cpp`. The current
   hardcoded expression is:
   `min(fuel_level - (wind_fire_alignment + distance_to_fire), unburnable_neighbour_count)`.

## Treatment CLI flags

| Flag | Type | Default | Meaning |
|---|---|---|---|
| `--TreatmentBudget` | int | `0` | Cells treated per fire period. `0` disables the hook entirely (fork is fully backward-compatible). |
| `--TreatmentDelay`  | int | `0` | Number of fire periods to skip before the first treatment is applied. |

Example (dogrib with a budget of 20 starting at fire period 1):
```
./Cell2Fire --input-instance-folder ../data/dogrib/ --output-folder ../results/dogrib_treated \
  --ignitions --sim-years 1 --nsims 1 --grids --final-grid --Fire-Period-Length 1.0 \
  --weather rows --nweathers 1 --output-messages --ROS-CV 0.0 --seed 123 --IgnitionRad 0 \
  --HFactor 1.0 --FFactor 1.0 --BFactor 1.0 --EFactor 1.0 \
  --TreatmentBudget 20 --TreatmentDelay 1
```

## Status

| Component | Status |
|---|---|
| Build / CI on Python 3.12 (pip, no conda) | done |
| Dockerfile (libgl1, libglib2.0-0, bind-mount workflow) | done |
| `Treated` cell state + receiver-side firebreak guard | done |
| Per-timestep `ApplyTreatments()` hook | done |
| Feature extraction (fuel, dist-to-fire, wind alignment, unburnable neighbours) | done |
| CLI args for treatment budget `K` and intervention delay `N` | done |

See `CLAUDE.md` for the full implementation spec.

# Introduction

A more actively maintained fork of this repository is [C2FK](https://github.com/fire2a/C2FK).

Cell2Fire is a new cell-based forest and wildland landscape fire spread simulator.
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
## C ++
Only simulation and generate evolution grids (no stats or plots).
Parallel-ready version will be uploaded soon.
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
