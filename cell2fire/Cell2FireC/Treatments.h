#ifndef TREATMENTS_H
#define TREATMENTS_H

#include "FBP5.0.h"
#include "ReadArgs.h"
#include "ReadCSV.h"

#include <unordered_set>
#include <vector>

// Per-cell features fed to the scoring expression. Mirrors the wildfireGP
// feature set so an expression evolved over there validates here with matching
// semantics. To add or remove a feature, edit this struct and `scoreCell` in
// Treatments.cpp; nothing else changes at the call sites.
struct Features {
    double fuel_level;                  // Crown Fuel Load normalized to [0,1] across cells
    double elevation;                   // df[i].elev normalized to [0,1] across cells
    double distance_to_fire;            // Chebyshev hops, 8-conn unrestricted; +inf if no fire
    double burnable_distance_to_fire;   // BFS hops through Available cells only; +inf if unreachable
    double wind_fire_alignment;         // bare cosine in [-1,1] (no wind-speed scaling)
    double has_treated_neighbour;       // 1.0 if any 8-neighbour is Treated, else 0.0
    double unburnable_neighbour_count;  // 8-conn count of Burnt/Harvested/Non-Burnable/Treated
};

// Normalize Crown Fuel Load from fuel_coefs lookup to [0,1] across all cells.
// Output vector is resized to nCells.
void precomputeFuelLevels(std::vector<double>& fuelLevels,
                          const inputs* df, fuel_coefs* coefs_base,
                          int nCells);

// Normalize df[i].elev to [0,1] across all cells. Output vector resized to nCells.
void precomputeElevations(std::vector<double>& elevations,
                          const inputs* df, int nCells);

// Reactive treatment hook. Scores every cell still in availCells, marks the
// top `budget` as Treated (status 5), erases them from availCells, and inserts
// into treatedCells. Returns the number of cells actually treated.
//
// The current scoring expression (see scoreCell in Treatments.cpp) is:
//   fuel_level + 3 * has_treated_neighbour + elevation - burnable_distance_to_fire
//
// `weather.waz` is the meteorological wind azimuth (direction wind comes FROM,
// degrees, 0=N). Verified empirically — see [[project-wind-convention-verified]].
int ApplyTreatments(std::unordered_set<int>& availCells,
                    std::unordered_set<int>& treatedCells,
                    std::vector<int>& statusCells,
                    const std::unordered_set<int>& burningCells,
                    const std::unordered_set<int>& burntCells,
                    const std::vector<double>& fuelLevels,
                    const std::vector<double>& elevations,
                    const weatherDF& weather,
                    int rows, int cols,
                    int budget);

#endif
