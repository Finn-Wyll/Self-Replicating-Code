#pragma once
// Shared helper for problems where the evolved program is a *priority rule*: at each decision
// the program is run once per candidate (seeing that candidate's sensor values) and the
// candidate with the highest output r0 is chosen. The evolved program is then a reusable rule
// ("earliest deadline first", "best value per weight", ...) that works on any new instance.
#include <adaptogen/sdk.hpp>

#include <cmath>
#include <limits>

namespace rules {

using namespace adaptogen::sdk;

/// Index (into candidates 0..n-1) with the highest program output. sense(candidate, channel, args)
/// answers the program's sensor reads for that candidate. Non-finite outputs rank lowest; ties go
/// to the lowest index. Returns -1 if n == 0.
template <class Sense>
int pick(Runner& run, int n, const Sense& sense) {
    int best = -1;
    double best_v = -std::numeric_limits<double>::infinity();
    for (int c = 0; c < n; ++c) {
        run.reset();
        auto env = make_env([&](int ch, const double* a, int k) { (void)k; return sense(c, ch, a); });
        run(env);
        double v = run.reg(0);
        if (!std::isfinite(v)) v = -std::numeric_limits<double>::infinity();
        if (best < 0 || v > best_v) {
            best = c;
            best_v = v;
        }
    }
    return best;
}

inline double mean(const std::vector<double>& v) {
    double s = 0;
    for (double x : v) s += x;
    return v.empty() ? 0.0 : s / static_cast<double>(v.size());
}

}  // namespace rules
