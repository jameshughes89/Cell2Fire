#include "Treatments.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

namespace {

// Total number of fuel types populated by setup_const() in FBPfunc5_NoDebug.c.
const int NUM_FUELS = 18;

double cflFor(const char* fueltype, fuel_coefs* base) {
    fuel_coefs* p = base;
    for (int i = 0; i < NUM_FUELS; ++i, ++p) {
        if (std::strncmp(p->fueltype, fueltype, 3) == 0) {
            return static_cast<double>(p->cfl);
        }
    }
    return 0.0;
}

// Hardcoded scoring expression:
//   min(fuel - (wind_align + dist_to_fire), unburnable_nbrs)
double scoreCell(double fuel, double wind_align, double dist_to_fire,
                 double unburnable_nbrs) {
    return std::min(fuel - (wind_align + dist_to_fire), unburnable_nbrs);
}

}  // namespace

void precomputeUnburnableNbrCounts(std::vector<int>& counts,
                                   const std::vector<int>& statusCells,
                                   int rows, int cols) {
    const int n = rows * cols;
    counts.assign(n, 0);
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            int idx = r * cols + c;
            int count = 0;
            // 4-connected: N, S, E, W (matches wildfireGP).
            if (r > 0 && statusCells[idx - cols] == 4) ++count;
            if (r < rows - 1 && statusCells[idx + cols] == 4) ++count;
            if (c > 0 && statusCells[idx - 1] == 4) ++count;
            if (c < cols - 1 && statusCells[idx + 1] == 4) ++count;
            counts[idx] = count;
        }
    }
}

void precomputeFuelLevels(std::vector<double>& fuelLevels,
                          const inputs* df, fuel_coefs* coefs_base,
                          int nCells) {
    fuelLevels.assign(nCells, 0.0);
    for (int i = 0; i < nCells; ++i) {
        fuelLevels[i] = cflFor(df[i].fueltype, coefs_base);
    }
}

int ApplyTreatments(std::unordered_set<int>& availCells,
                    std::unordered_set<int>& treatedCells,
                    std::vector<int>& statusCells,
                    const std::unordered_set<int>& burningCells,
                    const std::vector<std::vector<int>>& coordCells,
                    const std::vector<int>& unburnableNbrCounts,
                    const std::vector<double>& fuelLevels,
                    const weatherDF& weather,
                    int budget) {
    if (budget <= 0 || availCells.empty() || burningCells.empty()) {
        return 0;
    }

    // Wind vector: waz is the meteorological azimuth (direction wind comes FROM,
    // 0=N, 90=E). The vector pointing toward the wind source is (sin, cos) in
    // (east+, north+) coordinates. Scale by wind speed.
    const double DEG2RAD = M_PI / 180.0;
    const double waz_rad = static_cast<double>(weather.waz) * DEG2RAD;
    const double wind_vec_x = weather.ws * std::sin(waz_rad);
    const double wind_vec_y = weather.ws * std::cos(waz_rad);

    struct Candidate {
        int id;
        double score;
    };
    std::vector<Candidate> candidates;
    candidates.reserve(availCells.size());

    for (int id : availCells) {
        const int idx = id - 1;
        const double cellX = coordCells[idx][0];
        const double cellY = coordCells[idx][1];

        // Nearest burning cell (Euclidean).
        double bestDist2 = std::numeric_limits<double>::infinity();
        double bestDx = 0.0;
        double bestDy = 0.0;
        for (int bId : burningCells) {
            const int bidx = bId - 1;
            const double dx = coordCells[bidx][0] - cellX;
            const double dy = coordCells[bidx][1] - cellY;
            const double d2 = dx * dx + dy * dy;
            if (d2 < bestDist2) {
                bestDist2 = d2;
                bestDx = dx;
                bestDy = dy;
            }
        }

        const double dist = std::sqrt(bestDist2);
        double wind_align = 0.0;
        if (dist > 0.0) {
            const double inv = 1.0 / dist;
            wind_align = wind_vec_x * bestDx * inv + wind_vec_y * bestDy * inv;
        }

        const double fuel = fuelLevels[idx];
        const double unbNbrs = static_cast<double>(unburnableNbrCounts[idx]);
        candidates.push_back({id, scoreCell(fuel, wind_align, dist, unbNbrs)});
    }

    // Partial sort: only need the top `budget`.
    const int k = std::min<int>(budget, static_cast<int>(candidates.size()));
    std::partial_sort(candidates.begin(), candidates.begin() + k, candidates.end(),
                      [](const Candidate& a, const Candidate& b) {
                          return a.score > b.score;
                      });

    for (int i = 0; i < k; ++i) {
        const int id = candidates[i].id;
        statusCells[id - 1] = 5;
        availCells.erase(id);
        treatedCells.insert(id);
    }
    return k;
}
