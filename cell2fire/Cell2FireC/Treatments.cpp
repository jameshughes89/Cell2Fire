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

// Structural ablation variants (wildfireGP results/ablation_2026-10-02). Each mirrors the matching wildfireGP
// expression term for term, built on the C++ ports above so that only the ablated term differs.

// Distance-to-fire added outside the fire-front gate of the three policies that fail to transfer.
double scoreLowOnlyPlusDist(const Features& f) {
    return scoreCell3LowOnly(f) - f.burnable_distance_to_fire;
}

double scoreHillyPlusDist(const Features& f) {
    return scoreCell5Hilly(f) - f.burnable_distance_to_fire;
}

double scoreBarriersPlusDist(const Features& f) {
    return scoreCell6Barriers(f) - f.burnable_distance_to_fire;
}

// Distance-to-fire replaced by the constant 1 in the three transferring policies.
double scoreOpenAnchorNoDist(const Features& f) {
    const double denom = std::max(0.8, f.unburned_neighbour_count);
    return f.treated_neighbour_count + (f.mean_neighbour_fuel - 16.0) / denom;
}

double scoreHighOnlyNoDist(const Features& f) {
    const double t1 = f.wind_fire_alignment;
    const double t2 = pdiv(f.wind_fire_alignment, 1.0);
    const double t3 = pdiv(pdiv(f.wind_fire_alignment, f.burning_neighbour_count), 1.0);
    return std::min({t1, t2, t3}) + f.has_treated_neighbour - 1.0;
}

double scoreGaleNoDist(const Features& f) {
    return pdiv(
        f.has_treated_neighbour
            + pdiv(f.wind_fire_alignment, f.burning_neighbour_count)
            + f.mean_neighbour_fuel,
        1.0
    );
}

// open_anchor restricted to cells touching the fire, as in the frontier-gated policies.
double scoreOpenAnchorGated(const Features& f) {
    return (f.burning_neighbour_count > 0.0) ? scoreOpenAnchor(f) : -1000.0;
}

// Eq. 3 of the paper exactly as printed: T - 16 d / max(0.8, U).
double scoreOpenAnchorSimplified(const Features& f) {
    return f.treated_neighbour_count
         - pdiv(16.0 * f.burnable_distance_to_fire, std::max(0.8, f.unburned_neighbour_count));
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

FeatureContext buildFeatureContext(const std::vector<int>& statusCells,
                                   const std::unordered_set<int>& burningCells,
                                   const std::unordered_set<int>& burntCells,
                                   const std::vector<double>& fuelLevels,
                                   const std::vector<double>& elevations,
                                   const std::vector<double>& slopes,
                                   const weatherDF& weather,
                                   int rows, int cols) {
    const int nCells = rows * cols;
    FeatureContext ctx;
    ctx.rows = rows;
    ctx.cols = cols;
    ctx.statusCells = &statusCells;
    ctx.fuelLevels = &fuelLevels;
    ctx.elevations = &elevations;
    ctx.slopes = &slopes;

    // Cell2Fire tracks fire state in burningCells/burntCells, not statusCells: cells ignited by spread keep status 0.
    // burntCells includes cells that are still burning, so "burned out" is burnt and not burning.
    ctx.burning.assign(nCells, 0);
    ctx.burnedOut.assign(nCells, 0);
    for (int id : burningCells) ctx.burning[id - 1] = 1;
    for (int id : burntCells) {
        if (!ctx.burning[id - 1]) ctx.burnedOut[id - 1] = 1;
    }
    ctx.burningIdx.clear();
    for (int i = 0; i < nCells; ++i) {
        if (ctx.burning[i]) ctx.burningIdx.push_back(i);
    }

    // burnable_distance_to_fire: multi-source BFS from burning cells through unburned burnable cells only.
    ctx.burnableDist.assign(nCells, std::numeric_limits<int>::max());
    std::deque<int> queue;
    for (int b : ctx.burningIdx) {
        ctx.burnableDist[b] = 0;
        queue.push_back(b);
    }
    while (!queue.empty()) {
        const int cur = queue.front();
        queue.pop_front();
        const int r = cur / cols;
        const int c = cur % cols;
        for (int k = 0; k < 8; ++k) {
            const int nr = r + DR8[k];
            const int nc = c + DC8[k];
            if (nr < 0 || nr >= rows || nc < 0 || nc >= cols) continue;
            const int nIdx = nr * cols + nc;
            if (ctx.burnableDist[nIdx] != std::numeric_limits<int>::max()) continue;
            if (statusCells[nIdx] != 0 || ctx.burning[nIdx] || ctx.burnedOut[nIdx]) continue;
            ctx.burnableDist[nIdx] = ctx.burnableDist[cur] + 1;
            queue.push_back(nIdx);
        }
    }

    // waz is meteorological: direction wind comes FROM. Positive alignment
    // means the candidate cell is downwind of the fire.
    const double DEG2RAD = M_PI / 180.0;
    const double waz_rad = static_cast<double>(weather.waz) * DEG2RAD;
    ctx.wind_x = std::sin(waz_rad);
    ctx.wind_y = std::cos(waz_rad);
    return ctx;
}

Features computeFeatures(const FeatureContext& ctx, int idx) {
    const double INF = std::numeric_limits<double>::infinity();
    const int rows = ctx.rows;
    const int cols = ctx.cols;
    const std::vector<int>& statusCells = *ctx.statusCells;
    const std::vector<double>& fuelLevels = *ctx.fuelLevels;
    const std::vector<double>& elevations = *ctx.elevations;
    const int row = idx / cols;
    const int col = idx % cols;

    int bestDist = std::numeric_limits<int>::max();
    int bestFireIdx = -1;
    for (int bIdx : ctx.burningIdx) {
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
        if (mag > 0.0) wind_align = (ctx.wind_x * dx + ctx.wind_y * dy) / mag;
    }

    // Neighbourhood counts follow wildfireGP's NodeState semantics: non-fuel (4) and harvested (3) cells are
    // unburnable terrain that is still "unburned", and burned-out cells contribute zero fuel.
    int treated_count = 0;
    int burning_count = 0;
    int unburnable_count = 0;
    int unburned_count = 0;
    int neighbour_count = 0;
    double neighbour_fuel_sum = 0.0;
    double neighbour_elev_sum = 0.0;
    for (int k = 0; k < 8; ++k) {
        const int nr = row + DR8[k];
        const int nc = col + DC8[k];
        if (nr < 0 || nr >= rows || nc < 0 || nc >= cols) continue;
        const int nIdx = nr * cols + nc;
        const int ns = statusCells[nIdx];
        const bool burning = ctx.burning[nIdx] != 0;
        const bool burnedOut = ctx.burnedOut[nIdx] != 0;
        const bool treated = (ns == 5);
        const bool nonBurnable = (ns == 3 || ns == 4);
        ++neighbour_count;
        if (treated) ++treated_count;
        if (burning) ++burning_count;
        if (burnedOut || treated || nonBurnable) ++unburnable_count;
        if (!burning && !burnedOut && !treated) ++unburned_count;
        neighbour_fuel_sum += burnedOut ? 0.0 : fuelLevels[nIdx];
        neighbour_elev_sum += elevations[nIdx];
    }

    Features f;
    f.fuel_level    = fuelLevels[idx];
    f.elevation     = elevations[idx];
    f.slope         = (*ctx.slopes)[idx];
    f.distance_to_fire = (bestDist == std::numeric_limits<int>::max())
                         ? INF : static_cast<double>(bestDist);
    f.burnable_distance_to_fire = (ctx.burnableDist[idx] == std::numeric_limits<int>::max())
                                  ? INF : static_cast<double>(ctx.burnableDist[idx]);
    f.wind_fire_alignment        = wind_align;
    f.has_treated_neighbour      = (treated_count > 0) ? 1.0 : 0.0;
    f.unburnable_neighbour_count = static_cast<double>(unburnable_count);
    f.mean_neighbour_fuel        = (neighbour_count > 0) ? neighbour_fuel_sum / neighbour_count : 0.0;
    f.mean_neighbour_elevation   = (neighbour_count > 0) ? neighbour_elev_sum / neighbour_count : 0.0;
    f.burning_neighbour_count    = static_cast<double>(burning_count);
    f.treated_neighbour_count    = static_cast<double>(treated_count);
    f.unburned_neighbour_count   = static_cast<double>(unburned_count);
    f.elevation_delta_to_fire    = (bestFireIdx >= 0 && bestFireIdx != idx)
                                   ? elevations[idx] - elevations[bestFireIdx] : 0.0;
    return f;
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

    const double INF = std::numeric_limits<double>::infinity();
    const FeatureContext ctx = buildFeatureContext(statusCells, burningCells, burntCells, fuelLevels, elevations,
                                                   slopes, weather, rows, cols);
    const std::vector<int>& burnableDist = ctx.burnableDist;

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

    // Per-cell feature + score computation. Called for initial scoring and for
    // rescoring the 8 neighbours of a just-treated cell (the only cells whose
    // has_treated_neighbour / unburnable_neighbour_count can have changed).
    // Tuned-proximity ablation: "proxA_<w>" scores -d + w * has_treated_neighbour, "proxT_<w>" uses the count.
    const bool isProxA = strategy.rfind("proxA_", 0) == 0;
    const bool isProxT = strategy.rfind("proxT_", 0) == 0;
    const double proxWeight = (isProxA || isProxT) ? std::stod(strategy.substr(6)) : 0.0;

    auto computeScore = [&](int id) -> double {
        const Features f = computeFeatures(ctx, id - 1);

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
        else if (isProxA)                        s = -f.burnable_distance_to_fire + proxWeight * f.has_treated_neighbour;
        else if (isProxT)                        s = -f.burnable_distance_to_fire + proxWeight * f.treated_neighbour_count;
        else if (strategy == "lowonly_plusdist") s = scoreLowOnlyPlusDist(f);
        else if (strategy == "hilly_plusdist")   s = scoreHillyPlusDist(f);
        else if (strategy == "barriers_plusdist")s = scoreBarriersPlusDist(f);
        else if (strategy == "oa_nodist")        s = scoreOpenAnchorNoDist(f);
        else if (strategy == "hb_nodist")        s = scoreHighOnlyNoDist(f);
        else if (strategy == "ga_nodist")        s = scoreGaleNoDist(f);
        else if (strategy == "oa_gated")         s = scoreOpenAnchorGated(f);
        else if (strategy == "oa_simplified")    s = scoreOpenAnchorSimplified(f);
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
