#include "Treatments.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <deque>
#include <limits>
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

double scoreCell(const Features& f) {
    return f.fuel_level
         + 3.0 * f.has_treated_neighbour
         + f.elevation
         - f.burnable_distance_to_fire;
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
                    int budget) {
    if (budget <= 0 || availCells.empty() || burningCells.empty()) {
        return 0;
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

    struct Candidate {
        int id;
        double score;
    };
    std::vector<Candidate> candidates;
    candidates.reserve(availCells.size());

    for (int id : availCells) {
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
            if (d < bestDist) {
                bestDist = d;
                bestFireIdx = bIdx;
            }
        }

        double wind_align = 0.0;
        if (bestFireIdx >= 0 && bestFireIdx != idx) {
            const double dx = static_cast<double>(bestFireIdx % cols - col);
            // row increases southward, so flip sign for north-positive math frame
            const double dy = static_cast<double>(row - bestFireIdx / cols);
            const double mag = std::sqrt(dx * dx + dy * dy);
            if (mag > 0.0) {
                wind_align = (wind_x * dx + wind_y * dy) / mag;
            }
        }

        double has_treated = 0.0;
        int unburnable_count = 0;
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
        }

        Features f;
        f.fuel_level = fuelLevels[idx];
        f.elevation = elevations[idx];
        f.distance_to_fire = (bestDist == std::numeric_limits<int>::max())
                             ? INF : static_cast<double>(bestDist);
        f.burnable_distance_to_fire = (burnableDist[idx] == std::numeric_limits<int>::max())
                                      ? INF : static_cast<double>(burnableDist[idx]);
        f.wind_fire_alignment = wind_align;
        f.has_treated_neighbour = has_treated;
        f.unburnable_neighbour_count = static_cast<double>(unburnable_count);

        candidates.push_back({id, scoreCell(f)});
    }

    // All K picks see the same pre-step snapshot; has_treated_neighbour only
    // reflects treatments from prior steps, not earlier picks within this call.
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
