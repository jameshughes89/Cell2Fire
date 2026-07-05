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

double scoreShieldedRatio(const Features& f) {
    const double num = f.mean_neighbour_fuel
                     - f.burnable_distance_to_fire
                     - f.burning_neighbour_count / 18.0
                     + f.has_treated_neighbour;
    const double den = f.burnable_distance_to_fire + 2.0 * f.mean_neighbour_fuel;
    return num / den;
}


double scoreOpenAnchor(const Features& f) {
    const double denom = std::max(0.8, f.unburned_neighbour_count);
    return f.treated_neighbour_count
         + (f.mean_neighbour_fuel - 16.0) / denom * f.burnable_distance_to_fire;
}

double scoreFuelFlank(const Features& f) {
    const double fuel_term = (f.burning_neighbour_count > 0.0)
                             ? f.mean_neighbour_fuel / f.burning_neighbour_count
                             : 1.0;
    return fuel_term + f.has_treated_neighbour - f.distance_to_fire;
}

// Protected division: returns 1.0 when denominator is zero (GP convention).
inline double pdiv(double a, double b) {
    return (b == 0.0) ? 1.0 : a / b;
}

// GP-evolved strategies (one per training condition).

double scoreCell1Baseline(const Features& f) {
    return f.fuel_level + f.has_treated_neighbour - 3.0 * f.burnable_distance_to_fire;
}

double scoreCell2Ground(const Features& f) {
    if (f.unburnable_neighbour_count > 0.0)
        return 2.0 * f.mean_neighbour_fuel + f.has_treated_neighbour - f.burnable_distance_to_fire;
    else
        return f.mean_neighbour_fuel + f.has_treated_neighbour - f.burnable_distance_to_fire - 18.56;
}

double scoreCell3LowOnly(const Features& f) {
    if (f.burning_neighbour_count > 0.0)
        return 1.0 + f.mean_neighbour_fuel / f.burning_neighbour_count + f.has_treated_neighbour;
    else
        return 0.0;
}

double scoreCell4HighOnly(const Features& f) {
    const double t1 = f.wind_fire_alignment;
    const double t2 = pdiv(f.wind_fire_alignment, f.burnable_distance_to_fire);
    const double t3 = pdiv(pdiv(f.wind_fire_alignment, f.burning_neighbour_count),
                           f.burnable_distance_to_fire);
    const double min_term = std::min({t1, t2, t3});
    return min_term + f.has_treated_neighbour - f.burnable_distance_to_fire;
}

double scoreCell5Hilly(const Features& f) {
    if (f.burning_neighbour_count > 0.0)
        return f.treated_neighbour_count;
    else
        return -f.slope;
}

double scoreCell6Barriers(const Features& f) {
    if (f.burning_neighbour_count > 0.0)
        return pdiv(f.has_treated_neighbour, f.burning_neighbour_count)
               - f.mean_neighbour_elevation + 14.58;
    else
        return 0.0;
}

double scoreCell7Cranked(const Features& f) {
    return pdiv(
        f.has_treated_neighbour
            + pdiv(f.wind_fire_alignment, f.burning_neighbour_count)
            + f.mean_neighbour_fuel,
        f.burnable_distance_to_fire
    );
}

// Doctrine baselines ported from wildfireGP/strategies.py.
// MAX_ENGAGEMENT_DIST matches the max_distance=10 default used there.
static const double MAX_ENGAGEMENT_DIST = 10.0;
static const double OUT_OF_RANGE = -1.0e9;

double scoreFuelOnly(const Features& f) {
    return f.fuel_level + ANCHOR_WEIGHT * f.has_treated_neighbour;
}

double scoreBurningNbrs(const Features& f) {
    return f.burning_neighbour_count + ANCHOR_WEIGHT * f.has_treated_neighbour;
}

double scoreHeadFire(const Features& f) {
    if (f.distance_to_fire > MAX_ENGAGEMENT_DIST) return OUT_OF_RANGE;
    return f.wind_fire_alignment / (1.0 + f.distance_to_fire)
           + ANCHOR_WEIGHT * f.has_treated_neighbour;
}

double scoreFireRun(const Features& f) {
    if (f.distance_to_fire > MAX_ENGAGEMENT_DIST) return OUT_OF_RANGE;
    return f.fuel_level * f.wind_fire_alignment / (1.0 + f.distance_to_fire)
           + ANCHOR_WEIGHT * f.has_treated_neighbour;
}

double scoreFlankAttack(const Features& f) {
    if (f.distance_to_fire > MAX_ENGAGEMENT_DIST) return OUT_OF_RANGE;
    return f.fuel_level * (1.0 - std::abs(f.wind_fire_alignment)) / (1.0 + f.distance_to_fire)
           + ANCHOR_WEIGHT * f.has_treated_neighbour;
}

double scoreCompositeThreat(const Features& f) {
    if (f.distance_to_fire > MAX_ENGAGEMENT_DIST) return OUT_OF_RANGE;
    return f.fuel_level * f.slope * std::max(f.wind_fire_alignment, 0.0)
           / (1.0 + f.distance_to_fire)
           + ANCHOR_WEIGHT * f.has_treated_neighbour;
}

double scoreRidgeline(const Features& f) {
    if (f.distance_to_fire > MAX_ENGAGEMENT_DIST) return OUT_OF_RANGE;
    return f.elevation + f.slope + ANCHOR_WEIGHT * f.has_treated_neighbour;
}

double scoreIndirectAttack(const Features& f) {
    if (f.distance_to_fire > MAX_ENGAGEMENT_DIST) return OUT_OF_RANGE;
    return f.mean_neighbour_fuel / (1.0 + f.distance_to_fire)
           + ANCHOR_WEIGHT * f.has_treated_neighbour;
}

double scoreAnchorFlank(const Features& f) {
    if (f.distance_to_fire > MAX_ENGAGEMENT_DIST) return OUT_OF_RANGE;
    const double barriers = f.unburnable_neighbour_count + f.treated_neighbour_count;
    return f.fuel_level * barriers / (1.0 + f.distance_to_fire);
}

double scoreFrontierProtect(const Features& f) {
    if (f.burning_neighbour_count == 0.0) return OUT_OF_RANGE;
    return f.unburned_neighbour_count;
}

double scoreFrontierAnchored(const Features& f) {
    if (f.burning_neighbour_count == 0.0) return OUT_OF_RANGE;
    return f.unburned_neighbour_count + ANCHOR_WEIGHT * f.has_treated_neighbour;
}

double scoreUphillIntercept(const Features& f) {
    if (f.distance_to_fire > MAX_ENGAGEMENT_DIST) return OUT_OF_RANGE;
    return std::max(f.elevation_delta_to_fire, 0.0) / (1.0 + f.distance_to_fire)
           + ANCHOR_WEIGHT * f.has_treated_neighbour;
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

void precomputeSlopes(std::vector<double>& slopes,
                      const inputs* df, int nCells) {
    slopes.assign(nCells, 0.0);
    for (int i = 0; i < nCells; ++i) {
        slopes[i] = static_cast<double>(df[i].ps);
    }
    normalizeInPlace(slopes);
}

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
                    int minDist) {
    if (strategy == "none") return 0;
    if (budget <= 0 || availCells.empty() || burningCells.empty()) return 0;

    const int nCells = rows * cols;
    const double INF = std::numeric_limits<double>::infinity();

    // Multi-source BFS from burning cells through Available-only cells.
    // Used both for minDist filtering and for burnable_distance_to_fire feature.
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

    if (strategy == "random") {
        std::vector<int> ids;
        ids.reserve(availCells.size());
        for (int id : availCells) {
            if (burnableDist[id - 1] >= minDist) ids.push_back(id);
        }
        std::shuffle(ids.begin(), ids.end(), generator);
        const int k = std::min<int>(budget, static_cast<int>(ids.size()));
        for (int i = 0; i < k; ++i) {
            statusCells[ids[i] - 1] = 5;
            availCells.erase(ids[i]);
            treatedCells.insert(ids[i]);
        }
        return k;
    }

    // waz is meteorological: direction wind comes FROM. Positive alignment
    // means the candidate cell is downwind of the fire.
    const double DEG2RAD = M_PI / 180.0;
    const double waz_rad = static_cast<double>(weather.waz) * DEG2RAD;
    const double wind_x = std::sin(waz_rad);
    const double wind_y = std::cos(waz_rad);

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

        int treated_count = 0;
        int burning_count = 0;
        int unburnable_count = 0;
        double neighbour_fuel_sum = 0.0;
        double neighbour_elev_sum = 0.0;
        int neighbour_fuel_count = 0;
        for (int k = 0; k < 8; ++k) {
            const int nr = row + DR8[k];
            const int nc = col + DC8[k];
            if (nr < 0 || nr >= rows || nc < 0 || nc >= cols) continue;
            const int nIdx = nr * cols + nc;
            const int ns = statusCells[nIdx];
            if (ns == 5) ++treated_count;
            if (ns == 1) ++burning_count;
            // statusCells isn't updated to 2 for burnt cells; check burntCells directly
            const bool unburnable = (ns >= 3) || (burntCells.count(nIdx + 1) > 0);
            if (unburnable) ++unburnable_count;
            if (ns == 0) {
                neighbour_fuel_sum += fuelLevels[nIdx];
                neighbour_elev_sum += elevations[nIdx];
                ++neighbour_fuel_count;
            }
        }

        Features f;
        f.fuel_level    = fuelLevels[idx];
        f.elevation     = elevations[idx];
        f.slope         = slopes[idx];
        f.distance_to_fire = (bestDist == std::numeric_limits<int>::max())
                             ? INF : static_cast<double>(bestDist);
        f.burnable_distance_to_fire = (burnableDist[idx] == std::numeric_limits<int>::max())
                                      ? INF : static_cast<double>(burnableDist[idx]);
        f.wind_fire_alignment        = wind_align;
        f.has_treated_neighbour      = (treated_count > 0) ? 1.0 : 0.0;
        f.unburnable_neighbour_count = static_cast<double>(unburnable_count);
        f.mean_neighbour_fuel        = (neighbour_fuel_count > 0)
                                       ? neighbour_fuel_sum / neighbour_fuel_count : 0.0;
        f.mean_neighbour_elevation   = (neighbour_fuel_count > 0)
                                       ? neighbour_elev_sum / neighbour_fuel_count : 0.0;
        f.burning_neighbour_count    = static_cast<double>(burning_count);
        f.treated_neighbour_count    = static_cast<double>(treated_count);
        f.unburned_neighbour_count   = static_cast<double>(neighbour_fuel_count);
        f.elevation_delta_to_fire    = (bestFireIdx >= 0)
                                       ? elevations[idx] - elevations[bestFireIdx] : 0.0;

        double s;
        if (strategy == "proximity")            s = scoreProximity(f);
        else if (strategy == "neighbour_fuel")  s = scoreNeighbourFuel(f);
        else if (strategy == "shielded_ratio")  s = scoreShieldedRatio(f);
        else if (strategy == "open_anchor")     s = scoreOpenAnchor(f);
        else if (strategy == "fuel_flank")      s = scoreFuelFlank(f);
        else if (strategy == "cell1_baseline")  s = scoreCell1Baseline(f);
        else if (strategy == "cell2_ground")    s = scoreCell2Ground(f);
        else if (strategy == "cell3_lowonly")   s = scoreCell3LowOnly(f);
        else if (strategy == "cell4_highonly")  s = scoreCell4HighOnly(f);
        else if (strategy == "cell5_hilly")     s = scoreCell5Hilly(f);
        else if (strategy == "cell6_barriers")  s = scoreCell6Barriers(f);
        else if (strategy == "cell7_cranked")    s = scoreCell7Cranked(f);
        else if (strategy == "fuel_only")        s = scoreFuelOnly(f);
        else if (strategy == "burning_nbrs")     s = scoreBurningNbrs(f);
        else if (strategy == "head_fire")        s = scoreHeadFire(f);
        else if (strategy == "fire_run")         s = scoreFireRun(f);
        else if (strategy == "flank_attack")     s = scoreFlankAttack(f);
        else if (strategy == "composite_threat") s = scoreCompositeThreat(f);
        else if (strategy == "ridgeline")        s = scoreRidgeline(f);
        else if (strategy == "indirect_attack")  s = scoreIndirectAttack(f);
        else if (strategy == "anchor_flank")     s = scoreAnchorFlank(f);
        else if (strategy == "frontier_protect") s = scoreFrontierProtect(f);
        else if (strategy == "frontier_anchored")s = scoreFrontierAnchored(f);
        else if (strategy == "uphill_intercept") s = scoreUphillIntercept(f);
        else                                     s = scoreFuelElevation(f);
        return std::isfinite(s) ? s : -INF;
    };

    // Shuffle first so equal-scoring candidates are broken randomly; stable_sort
    // preserves that order through re-sorts after each placement.
    // Honour minDist: exclude cells within minDist BFS hops of the fire front.
    std::vector<int> ids;
    ids.reserve(availCells.size());
    for (int id : availCells) {
        if (burnableDist[id - 1] >= minDist) ids.push_back(id);
    }
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
