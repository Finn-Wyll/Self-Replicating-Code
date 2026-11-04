#include "adaptogen/problem.hpp"

#include "adaptogen/parallel.hpp"

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

void Problem::evaluate_batch(const MachineSpec& spec, const std::vector<const Program*>& programs, Split split,
                             const std::vector<std::size_t>& cases, int threads, std::vector<double>& scores,
                             std::vector<long>& steps) const {
    const std::size_t n = cases.size();
    scores.assign(programs.size() * n, 0.0);
    steps.assign(programs.size(), 0);
    parallel_for(programs.size(), threads, [&](std::size_t p) {
        Runner runner(spec);
        for (std::size_t k = 0; k < n; ++k) scores[p * n + k] = evaluate_case(runner, *programs[p], split, cases[k]);
        steps[p] = runner.steps();
    });
}

}  // namespace adaptogen
