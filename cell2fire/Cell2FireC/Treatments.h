#ifndef TREATMENTS_H
#define TREATMENTS_H

#include "FBP5.0.h"
#include "ReadArgs.h"
#include "ReadCSV.h"

#include <unordered_set>
#include <vector>

// Precompute the 4-connected unburnable neighbour count for every cell.
// Output vector is resized to nCells.
void precomputeUnburnableNbrCounts(std::vector<int>& counts,
                                   const std::vector<int>& statusCells,
                                   int rows, int cols);

// Precompute per-cell fuel level using the Crown Fuel Load from fuel_coefs.
// Non-burnable cells get 0. Output vector is resized to nCells.
void precomputeFuelLevels(std::vector<double>& fuelLevels,
                          const inputs* df, fuel_coefs* coefs_base,
                          int nCells);

// Reactive treatment hook. Scores every cell still in availCells, marks the
// top `budget` as Treated (status 5), erases them from availCells, and inserts
// into treatedCells. Returns the number of cells actually treated.
//
// The current hardcoded scoring expression is:
//   min(fuel_level - (wind_fire_alignment + distance_to_fire),
//       unburnable_neighbour_count)
//
// `weather` is the weather row for the current period; waz is interpreted as
// the meteorological wind azimuth (direction wind comes FROM, degrees, 0=N).
int ApplyTreatments(std::unordered_set<int>& availCells,
                    std::unordered_set<int>& treatedCells,
                    std::vector<int>& statusCells,
                    const std::unordered_set<int>& burningCells,
                    const std::vector<std::vector<int>>& coordCells,
                    const std::vector<int>& unburnableNbrCounts,
                    const std::vector<double>& fuelLevels,
                    const weatherDF& weather,
                    int budget);

#endif
