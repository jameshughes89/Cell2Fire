# Cell2Fire — GP Validation Fork

## Purpose

This is a fork of [cell2fire/Cell2Fire](https://github.com/cell2fire/Cell2Fire) extended to
validate GP-evolved wildfire suppression strategies from
[wildfireGP](https://github.com/jameshughes89/wildfireGP).

The original Cell2Fire is a batch fire spread simulator with no reactive treatment support.
This fork adds per-timestep treatment allocation so that a GP-evolved scoring function can be
applied each timestep during simulation — mirroring how strategies are evaluated in wildfireGP
but on a physically realistic spread model (Canadian FBP fuel system, elliptical spread,
real weather inputs).

## Background

In wildfireGP, GP evolves tree-based programs that score each node per timestep. The
highest-scoring unburned burnable nodes are treated (firebreak placed) before each spread step.
The dominant evolved expression from the first full run (pop=500, gens=100) is:

```
min(sub(fuel_level, add(wind_fire_alignment, distance_to_fire)), unburnable_neighbour_count)
```

In plain terms: prioritise high-fuel nodes that are not downwind of the fire and are adjacent
to natural firebreaks. This reduced burned area by ~39% vs no treatment on the wildfireGP
simulator (mean 1232 vs 2034 burned cells, 100-run evaluation).

## What Needs to Be Built

### 1. Treatment state in the spread model

Cell2Fire has cell states: 0=Available, 1=Burning, 2=Burnt, 3=Harvested, 4=Non-Burnable.
Add state 5=Treated (or reuse Harvested=3 if it already blocks spread — verify this first).
Treated cells must not ignite when receiving fire messages.

Hook: `CellsFBP::manageFire()` in `cell2fire/Cell2FireC/CellsFBP.cpp` — this is where
neighbour ignition is decided. Add a check: if target cell is Treated, skip ignition.

### 2. Per-timestep intervention hook

The main simulation loop is in `cell2fire/Cell2FireC/Cell2Fire.cpp` around line 1431:

```cpp
for (tstep = 0; tstep <= MaxFirePeriods * TotalYears; tstep++) {
    Forest.Step(generator);
    if (Forest.done) break;
}
```

After `Forest.Step()` (or inside `Step()` after state updates, before message passing), add
a call to an `ApplyTreatments()` function that:
- Iterates over all Available cells (use `Forest.availCells` unordered_set)
- Scores each using the GP expression
- Sorts by score descending
- Marks the top K as Treated (state=5)
- Respects an intervention delay (no treatments for first N steps)

Treatment budget K and intervention delay N should be CLI arguments.

### 3. Feature extraction

The GP expression needs four features per cell. All data is available in the existing C++
data structures:

**fuel_level**: `df[cellID].fueltype` or the ROS value — check what best represents
"fuel available to burn" in the FBP system. Likely the Crown Fuel Load or surface fuel.

**distance_to_fire**: BFS or Euclidean distance from each Available cell to the nearest
Burning cell. `Forest.burningCells` is an unordered_set of currently burning cell IDs.
Compute once per timestep before scoring. Euclidean using grid (row,col) coordinates is
sufficient.

**wind_fire_alignment**: Dot product of the wind vector with the unit vector from the cell
toward the nearest burning cell. Wind direction and speed are in `wdf[weatherPeriod]`
(fields `waz` for azimuth in degrees, `ws` for speed). High alignment = cell is downwind
of fire = fire is heading toward it fast.

**unburnable_neighbour_count**: Count of Non-Burnable (state=4) neighbours. Static —
precompute once before simulation starts and store per cell. Uses the grid adjacency
(4-connected or 8-connected, match wildfireGP which uses 4-connected).

### 4. Scoring function

Once features are computed, the GP expression is:

```cpp
double score(double fuel, double wind_align, double dist_to_fire, double unburnable_nbrs) {
    return std::min(fuel - (wind_align + dist_to_fire), unburnable_nbrs);
}
```

This is the expression from the first wildfireGP run. Replace with whichever expression is
selected after the factorial experiments complete.

## Key Files

- `cell2fire/Cell2FireC/Cell2Fire.cpp` — main simulation loop (~1450 lines)
- `cell2fire/Cell2FireC/CellsFBP.cpp` — per-cell fire behaviour (~962 lines)
- `cell2fire/Cell2FireC/Cell2Fire.h` — Cell2Fire class, holds availCells/burningCells/burntCells
- `cell2fire/Cell2FireC/DataGenerator.h` — input data structures (df[], wdf[])

## Style

- Match the existing C++ style (no enforced formatter visible — follow surrounding code)
- Keep the intervention logic in a dedicated `Treatments.cpp` / `Treatments.h` rather than
  adding directly to Cell2Fire.cpp to keep the diff reviewable
- Add CLI args following the pattern in `cell2fire/Cell2FireC/ReadArgs.cpp`

## Relationship to wildfireGP

wildfireGP (~/Desktop/wildfireGP) is where GP training and analysis happens. This repo is
validation only. The workflow is:

1. Run GP in wildfireGP, select best expression via batch_evaluate.py
2. Hardcode expression in this repo's scoring function
3. Run Cell2Fire with treatment enabled, compare burn outcomes to no-treatment baseline
4. Report results alongside wildfireGP results in the paper
