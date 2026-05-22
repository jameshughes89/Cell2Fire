#include "Treatments.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <deque>
#include <limits>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

const int NUM_FUELS = 18;

const int DR8[8] = {-1, -1, -1, 0, 0, 1, 1, 1};
const int DC8[8] = {-1,  0,  1, -1, 1, -1, 0, 1};

double cflFor(const char* fueltype, fuel_coefs* base) {
    fuel_coefs* p = base;
    for (int i = 0; i < NUM_FUELS; ++i, ++p) {
        if (std::strncmp(p->fueltype, fueltype, 3) == 0) {
            return static_cast<double>(p->cfl);
        }
    }
    return 0.0;
}

void normalizeInPlace(std::vector<double>& v) {
    if (v.empty()) return;
    double lo = v[0], hi = v[0];
    for (double x : v) {
        if (x < lo) lo = x;
        if (x > hi) hi = x;
    }
    const double range = hi - lo;
    if (range <= 0.0) {
        std::fill(v.begin(), v.end(), 0.0);
        return;
    }
    for (double& x : v) {
        x = (x - lo) / range;
    }
}

double scoreFuelElevation(const Features& f) {
    return f.fuel_level
         + 3.0 * f.has_treated_neighbour
         + f.elevation
         - f.burnable_distance_to_fire;
}

double scoreNeighbourFuel(const Features& f) {
    return -f.burnable_distance_to_fire
           + f.mean_neighbour_fuel
           + f.has_treated_neighbour;
}

const double ANCHOR_WEIGHT = 0.1;

double scoreProximity(const Features& f) {
    return -f.burnable_distance_to_fire + ANCHOR_WEIGHT * f.has_treated_neighbour;
}

}  // namespace

void precomputeFuelLevels(std::vector<double>& fuelLevels,
                          const inputs* df, fuel_coefs* coefs_base,
                          int nCells) {
    fuelLevels.assign(nCells, 0.0);
    for (int i = 0; i < nCells; ++i) {
        fuelLevels[i] = cflFor(df[i].fueltype, coefs_base);
    }
    normalizeInPlace(fuelLevels);
}

void precomputeElevations(std::vector<double>& elevations,
                          const inputs* df, int nCells) {
    elevations.assign(nCells, 0.0);
    for (int i = 0; i < nCells; ++i) {
        elevations[i] = static_cast<double>(df[i].elev);
    }
    normalizeInPlace(elevations);
}

int ApplyTreatments(std::unordered_set<int>& availCells,
                    std::unordered_set<int>& treatedCells,
                    std::vector<int>& statusCells,
                    const std::unordered_set<int>& burningCells,
                    const std::unordered_set<int>& burntCells,
                    const std::vector<double>& fuelLevels,
                    const std::vector<double>& elevations,
                    const weatherDF& weather,
                    int rows, int cols,
                    int budget,
                    const std::string& strategy,
                    std::default_random_engine& generator) {
    if (strategy == "none") return 0;
    if (budget <= 0 || availCells.empty() || burningCells.empty()) return 0;

    if (strategy == "random") {
        std::vector<int> ids(availCells.begin(), availCells.end());
        std::shuffle(ids.begin(), ids.end(), generator);
        const int k = std::min<int>(budget, static_cast<int>(ids.size()));
        for (int i = 0; i < k; ++i) {
            statusCells[ids[i] - 1] = 5;
            availCells.erase(ids[i]);
            treatedCells.insert(ids[i]);
        }
        return k;
    }

    const int nCells = rows * cols;
    const double INF = std::numeric_limits<double>::infinity();

    // waz is meteorological: direction wind comes FROM. Positive alignment
    // means the candidate cell is downwind of the fire.
    const double DEG2RAD = M_PI / 180.0;
    const double waz_rad = static_cast<double>(weather.waz) * DEG2RAD;
    const double wind_x = std::sin(waz_rad);
    const double wind_y = std::cos(waz_rad);

    // Multi-source BFS from burning cells through Available-only cells.
    std::vector<int> burnableDist(nCells, std::numeric_limits<int>::max());
    {
        std::deque<int> queue;
        for (int bId : burningCells) {
            burnableDist[bId - 1] = 0;
            queue.push_back(bId - 1);
        }
        while (!queue.empty()) {
            const int cur = queue.front();
            queue.pop_front();
            const int curDist = burnableDist[cur];
            const int r = cur / cols;
            const int c = cur % cols;
            for (int k = 0; k < 8; ++k) {
                const int nr = r + DR8[k];
                const int nc = c + DC8[k];
                if (nr < 0 || nr >= rows || nc < 0 || nc >= cols) continue;
                const int nIdx = nr * cols + nc;
                if (burnableDist[nIdx] != std::numeric_limits<int>::max()) continue;
                if (statusCells[nIdx] != 0) continue;
                burnableDist[nIdx] = curDist + 1;
                queue.push_back(nIdx);
            }
        }
    }

    // Per-cell feature + score computation. Called for initial scoring and for
    // rescoring the 8 neighbours of a just-treated cell (the only cells whose
    // has_treated_neighbour / unburnable_neighbour_count can have changed).
    auto computeScore = [&](int id) -> double {
        const int idx = id - 1;
        const int row = idx / cols;
        const int col = idx % cols;

        int bestDist = std::numeric_limits<int>::max();
        int bestFireIdx = -1;
        for (int bId : burningCells) {
            const int bIdx = bId - 1;
            const int dr = std::abs(bIdx / cols - row);
            const int dc = std::abs(bIdx % cols - col);
            const int d = (dr > dc) ? dr : dc;
            if (d < bestDist) { bestDist = d; bestFireIdx = bIdx; }
        }

        double wind_align = 0.0;
        if (bestFireIdx >= 0 && bestFireIdx != idx) {
            const double dx = static_cast<double>(bestFireIdx % cols - col);
            // row increases southward, so flip sign for north-positive math frame
            const double dy = static_cast<double>(row - bestFireIdx / cols);
            const double mag = std::sqrt(dx * dx + dy * dy);
            if (mag > 0.0) wind_align = (wind_x * dx + wind_y * dy) / mag;
        }

        double has_treated = 0.0;
        int unburnable_count = 0;
        double neighbour_fuel_sum = 0.0;
        int neighbour_fuel_count = 0;
        for (int k = 0; k < 8; ++k) {
            const int nr = row + DR8[k];
            const int nc = col + DC8[k];
            if (nr < 0 || nr >= rows || nc < 0 || nc >= cols) continue;
            const int nIdx = nr * cols + nc;
            const int ns = statusCells[nIdx];
            if (ns == 5) has_treated = 1.0;
            // statusCells isn't updated to 2 for burnt cells; check burntCells directly
            const bool unburnable = (ns >= 3) || (burntCells.count(nIdx + 1) > 0);
            if (unburnable) ++unburnable_count;
            if (ns == 0) {
                neighbour_fuel_sum += fuelLevels[nIdx];
                ++neighbour_fuel_count;
            }
        }

        Features f;
        f.fuel_level    = fuelLevels[idx];
        f.elevation     = elevations[idx];
        f.distance_to_fire = (bestDist == std::numeric_limits<int>::max())
                             ? INF : static_cast<double>(bestDist);
        f.burnable_distance_to_fire = (burnableDist[idx] == std::numeric_limits<int>::max())
                                      ? INF : static_cast<double>(burnableDist[idx]);
        f.wind_fire_alignment        = wind_align;
        f.has_treated_neighbour      = has_treated;
        f.unburnable_neighbour_count = static_cast<double>(unburnable_count);
        f.mean_neighbour_fuel        = (neighbour_fuel_count > 0)
                                       ? neighbour_fuel_sum / neighbour_fuel_count : 0.0;

        double s;
        if (strategy == "proximity")       s = scoreProximity(f);
        else if (strategy == "neighbour_fuel") s = scoreNeighbourFuel(f);
        else                               s = scoreFuelElevation(f);
        return std::isfinite(s) ? s : -INF;
    };

    // Shuffle first so equal-scoring candidates are broken randomly; stable_sort
    // preserves that order through re-sorts after each placement.
    std::vector<int> ids(availCells.begin(), availCells.end());
    std::shuffle(ids.begin(), ids.end(), generator);

    std::unordered_map<int, double> scores;
    scores.reserve(ids.size());
    for (int id : ids) scores[id] = computeScore(id);

    std::unordered_set<int> candidateSet(ids.begin(), ids.end());

    // Sort ascending so pop_back() yields the highest scorer.
    std::stable_sort(ids.begin(), ids.end(),
        [&](int a, int b) { return scores[a] < scores[b]; });

    int treated = 0;
    while (treated < budget && !ids.empty()) {
        const int id = ids.back();
        ids.pop_back();
        candidateSet.erase(id);

        statusCells[id - 1] = 5;
        availCells.erase(id);
        treatedCells.insert(id);
        ++treated;

        // Rescore only the 8 neighbours whose features may have changed.
        const int idx = id - 1;
        const int row = idx / cols;
        const int col = idx % cols;
        bool any = false;
        for (int k = 0; k < 8; ++k) {
            const int nr = row + DR8[k];
            const int nc = col + DC8[k];
            if (nr < 0 || nr >= rows || nc < 0 || nc >= cols) continue;
            const int nId = nr * cols + nc + 1;
            if (candidateSet.count(nId)) {
                scores[nId] = computeScore(nId);
                any = true;
            }
        }
        if (any) {
            std::stable_sort(ids.begin(), ids.end(),
                [&](int a, int b) { return scores[a] < scores[b]; });
        }
    }
    return treated;
}
