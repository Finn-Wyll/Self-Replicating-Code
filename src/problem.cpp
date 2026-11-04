#include "adaptogen/problem.hpp"

namespace adaptogen {

double Problem::mean_score(const MachineSpec& spec, const Program& program, Split split) const {
    const std::size_t n = num_cases(split);
    if (n == 0) return 0.0;
    Runner runner(spec);
    double total = 0.0;
    for (std::size_t i = 0; i < n; ++i) total += evaluate_case(runner, program, split, i);
    return total / static_cast<double>(n);
}

std::vector<Metric> Problem::metrics(const MachineSpec& spec, const Program& program, Split split) const {
    return {{"mean_score", mean_score(spec, program, split)}};
}

}  // namespace adaptogen
