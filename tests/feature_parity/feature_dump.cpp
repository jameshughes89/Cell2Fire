// Dump the treatment hook's per-cell features for a fire state read from stdin.
//
// Input:  "rows cols waz ws reference_cells" then rows*cols lines of
//         "status burning burned_out fuel elevation slope" (row-major).
// Output: one line per cell: "idx", the 14 hand-coded/evolved-strategy Features fields in declaration order, then the
//         22-value learned-policy input (rlInputVector) for the given reference_cells.
// Used by tests/test_feature_parity.py to compare against wildfireGP's features.py and wildfireGP.rl.features.
#include "../../cell2fire/Cell2FireC/Treatments.cpp"

#include <iomanip>
#include <iostream>

int main() {
    int rows, cols, waz, referenceCells;
    double ws;
    std::cin >> rows >> cols >> waz >> ws >> referenceCells;
    const int nCells = rows * cols;
    std::vector<int> statusCells(nCells);
    std::vector<double> fuel(nCells), elev(nCells), slope(nCells);
    std::unordered_set<int> burningCells, burntCells;
    for (int i = 0; i < nCells; ++i) {
        int burning, burnedOut;
        std::cin >> statusCells[i] >> burning >> burnedOut >> fuel[i] >> elev[i] >> slope[i];
        if (burning) {
            burningCells.insert(i + 1);
            burntCells.insert(i + 1);
        }
        if (burnedOut) burntCells.insert(i + 1);
    }
    weatherDF weather{};
    weather.waz = waz;
    weather.ws = ws;

    const FeatureContext ctx =
        buildFeatureContext(statusCells, burningCells, burntCells, fuel, elev, slope, weather, rows, cols, true);
    std::cout << std::setprecision(17);
    float rl[RL_N_FEATURES];
    for (int i = 0; i < nCells; ++i) {
        const Features f = computeFeatures(ctx, i);
        std::cout << i << ' ' << f.fuel_level << ' ' << f.elevation << ' ' << f.slope << ' ' << f.distance_to_fire
                  << ' ' << f.burnable_distance_to_fire << ' ' << f.wind_fire_alignment << ' '
                  << f.has_treated_neighbour << ' ' << f.unburnable_neighbour_count << ' ' << f.mean_neighbour_fuel
                  << ' ' << f.mean_neighbour_elevation << ' ' << f.burning_neighbour_count << ' '
                  << f.treated_neighbour_count << ' ' << f.unburned_neighbour_count << ' '
                  << f.elevation_delta_to_fire;
        rlInputVector(f, ctx, referenceCells, rl);
        for (float v : rl) std::cout << ' ' << v;
        std::cout << '\n';
    }
}
