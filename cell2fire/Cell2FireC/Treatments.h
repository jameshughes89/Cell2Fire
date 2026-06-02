#ifndef TREATMENTS_H
#define TREATMENTS_H

#include "FBP5.0.h"
#include "ReadArgs.h"
#include "ReadCSV.h"

#include <random>
#include <string>
#include <unordered_set>
#include <vector>

struct Features {
    double fuel_level;                  // Crown Fuel Load, normalized [0,1]
    double elevation;                   // df[i].elev, normalized [0,1]
    double slope;                       // df[i].ps (percent slope), normalized [0,1]
    double distance_to_fire;            // Chebyshev hops, unrestricted 8-conn; +inf if no fire
    double burnable_distance_to_fire;   // BFS hops through Available cells only; +inf if unreachable
    double wind_fire_alignment;         // cosine in [-1,1], no wind-speed scaling
    double has_treated_neighbour;       // 1.0 if any 8-neighbour is Treated, else 0.0
    double unburnable_neighbour_count;  // 8-conn count of Burnt/Harvested/Non-Burnable/Treated
    double mean_neighbour_fuel;         // mean fuelLevel of Available 8-neighbours
    double mean_neighbour_elevation;    // mean normalized elevation of Available 8-neighbours
    double burning_neighbour_count;     // 8-conn count of Burning neighbours
    double treated_neighbour_count;     // 8-conn count of Treated neighbours
    double unburned_neighbour_count;    // 8-conn count of Available neighbours
};

void precomputeFuelLevels(std::vector<double>& fuelLevels,
                          const inputs* df, fuel_coefs* coefs_base,
                          int nCells);

void precomputeElevations(std::vector<double>& elevations,
                          const inputs* df, int nCells);

void precomputeSlopes(std::vector<double>& slopes,
                      const inputs* df, int nCells);

// Strategies: "fuel_elevation", "neighbour_fuel", "proximity", "shielded_ratio",
//             "open_anchor", "fuel_flank", "cell1_baseline", "cell2_ground",
//             "cell3_lowonly", "cell4_highonly", "cell5_hilly", "cell6_barriers",
//             "random", "none"
int ApplyTreatments(std::unordered_set<int>& availCells,
                    std::unordered_set<int>& treatedCells,
                    std::vector<int>& statusCells,
                    const std::unordered_set<int>& burningCells,
                    const std::unordered_set<int>& burntCells,
                    const std::vector<double>& fuelLevels,
                    const std::vector<double>& elevations,
                    const std::vector<double>& slopes,
                    const weatherDF& weather,
                    int rows, int cols,
                    int budget,
                    const std::string& strategy,
                    std::default_random_engine& generator,
                    int minDist = 0);

#endif
