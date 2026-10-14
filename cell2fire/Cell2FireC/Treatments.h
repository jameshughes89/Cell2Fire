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
    double burnable_distance_to_fire;   // BFS hops through unburned burnable cells; +inf if unreachable
    double wind_fire_alignment;         // cosine in [-1,1], no wind-speed scaling
    double has_treated_neighbour;       // 1.0 if any 8-neighbour is Treated, else 0.0
    double unburnable_neighbour_count;  // 8-conn count of burned-out/Harvested/Non-Burnable/Treated
    double mean_neighbour_fuel;         // mean fuelLevel of in-bounds 8-neighbours; burned-out = 0
    double mean_neighbour_elevation;    // mean normalized elevation of in-bounds 8-neighbours
    double burning_neighbour_count;     // 8-conn count of Burning neighbours
    double treated_neighbour_count;     // 8-conn count of Treated neighbours
    double unburned_neighbour_count;    // 8-conn count of not burning/burned-out/treated neighbours
    double elevation_delta_to_fire;     // elevation[cell] - elevation[nearest burning cell]; 0 if no fire
    // Computed only when the context is built with rlFeatures = true (learned-policy input); 0 otherwise.
    double burning_two_hop_count;       // burning cells at Chebyshev distance exactly 2
    double reachable_unburned_area;     // size of the cell's Moore-connected component of unburned burnable land
    double wind_speed;                  // km/h
    double fuel_moisture;               // fixed at wildfireGP's training value (RL_FUEL_MOISTURE)
    double total_burning;               // landscape-wide counts; "unburned" includes non-fuel, as in wildfireGP
    double total_burned;
    double total_unburned;
    double total_treated;
};

// Per-call fire state for feature extraction. Mirrors wildfireGP's NodeState: burning and burned-out come from
// Cell2Fire's burningCells/burntCells sets, treated (5), harvested (3) and non-fuel (4) from statusCells.
struct FeatureContext {
    int rows;
    int cols;
    const std::vector<int>* statusCells;
    const std::vector<double>* fuelLevels;
    const std::vector<double>* elevations;
    const std::vector<double>* slopes;
    std::vector<char> burning;
    std::vector<char> burnedOut;
    std::vector<int> burningIdx;    // ascending (row-major), matching wildfireGP's np.argwhere seed order
    std::vector<int> fireDist;      // Chebyshev distance to nearest burning cell; INT_MAX if no fire
    std::vector<int> nearestFire;   // index of that burning cell (wildfireGP BFS tie order); -1 if no fire
    std::vector<int> burnableDist;  // BFS hops through unburned burnable cells; INT_MAX if unreachable
    double wind_x;
    double wind_y;
    bool rlFeatures;                // whether the learned-policy features below were computed
    std::vector<int> twoHopBurning;
    std::vector<int> reachableArea;
    double windSpeed;
    int nBurning;
    int nBurnedOut;
    int nTreated;
};

FeatureContext buildFeatureContext(const std::vector<int>& statusCells,
                                   const std::unordered_set<int>& burningCells,
                                   const std::unordered_set<int>& burntCells,
                                   const std::vector<double>& fuelLevels,
                                   const std::vector<double>& elevations,
                                   const std::vector<double>& slopes,
                                   const weatherDF& weather,
                                   int rows, int cols,
                                   bool rlFeatures = false);

// Features of cell idx (0-based) under ctx, matching wildfireGP's features.py definitions.
Features computeFeatures(const FeatureContext& ctx, int idx);

// Learned-policy (e.g. reinforcement learning) input: the 22 features in wildfireGP.rl.features.FEATURE_ORDER, with
// that module's conventions. Unreachable distances become max(rows, cols). reachable_unburned_area and the four
// total_* counts are multiplied by referenceCells / (rows * cols), so a policy trained on a wildfireGP grid of
// referenceCells cells sees a larger landscape on its training scale; the default 2500 is the 50x50 wildfireGP grid,
// and referenceCells <= 0 leaves the counts unscaled. ctx must be built with rlFeatures = true.
const int RL_N_FEATURES = 22;
const int RL_DEFAULT_REFERENCE_CELLS = 2500;
const double RL_FUEL_MOISTURE = 0.2;
void rlInputVector(const Features& f, const FeatureContext& ctx, int referenceCells, float out[RL_N_FEATURES]);

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
//             "cell7_cranked", "fuel_only", "burning_nbrs", "head_fire",
//             "fire_run", "flank_attack", "composite_threat", "ridgeline",
//             "indirect_attack", "anchor_flank", "frontier_protect",
//             "frontier_anchored", "uphill_intercept", "random", "none",
//             "rl" (only in builds with a learned-policy scorer; see README)
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
                    int minDist = 0,
                    int rlReferenceCells = RL_DEFAULT_REFERENCE_CELLS);

#endif
