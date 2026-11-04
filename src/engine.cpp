#include "adaptogen/engine.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <chrono>
#include <numeric>
#include <thread>

#include "adaptogen/analysis.hpp"

namespace adaptogen {

const char* to_string(SelectionMethod m) { return m == SelectionMethod::Tournament ? "tournament" : "lexicase"; }

void parallel_for(std::size_t n, int threads, const std::function<void(std::size_t)>& fn) {
    unsigned t = threads > 0 ? static_cast<unsigned>(threads) : std::max(1u, std::thread::hardware_concurrency());
    t = static_cast<unsigned>(std::min<std::size_t>(t, n));
    if (t <= 1) {
        for (std::size_t i = 0; i < n; ++i) fn(i);
        return;
    }
    std::atomic<std::size_t> next{0};
    std::vector<std::thread> pool;
    pool.reserve(t);
    for (unsigned k = 0; k < t; ++k)
        pool.emplace_back([&] {
            for (std::size_t i; (i = next.fetch_add(1)) < n;) fn(i);
        });
    for (auto& th : pool) th.join();
}

MachineSpec make_machine(const Problem& problem, const EngineConfig& cfg) {
    MachineSpec spec;
    spec.num_registers = std::max(cfg.num_registers, problem.num_outputs());
    spec.ops = cfg.ops.empty() ? problem.default_ops() : cfg.ops;
    spec.channels = problem.channels();
    spec.limits = cfg.limits;
    spec.const_min = cfg.const_min;
    spec.const_max = cfg.const_max;
    spec.finalize();
    return spec;
}

Engine::Engine(const Problem& problem, EngineConfig cfg)
    : problem_(problem), cfg_(std::move(cfg)), spec_(make_machine(problem, cfg_)) {}

namespace {

struct Individual {
    Program program;
    std::vector<double> scores;
    double fitness = 0.0;
    double mean_steps = 0.0;
    bool evaluated = false;
};

double median_of(std::vector<double>& v) {
    if (v.empty()) return 0.0;
    auto mid = v.begin() + static_cast<long>(v.size() / 2);
    std::nth_element(v.begin(), mid, v.end());
    return *mid;
}

}  // namespace

EvolutionResult Engine::run() {
    using clock = std::chrono::steady_clock;
    const auto t0 = clock::now();
    auto elapsed = [&] { return std::chrono::duration<double>(clock::now() - t0).count(); };

    Rng rng(cfg_.seed);
    const std::size_t P = static_cast<std::size_t>(std::max(2, cfg_.population));
    const std::size_t C = problem_.num_cases(Split::Train);
    const int outputs = problem_.num_outputs();
    long evaluations = 0;

    auto evaluate = [&](std::vector<Individual>& pop) {
        parallel_for(pop.size(), cfg_.threads, [&](std::size_t i) {
            Individual& ind = pop[i];
            if (ind.evaluated) return;
            Runner runner(spec_);
            ind.scores.resize(C);
            double total = 0.0;
            for (std::size_t c = 0; c < C; ++c) total += ind.scores[c] = problem_.evaluate_case(runner, ind.program, Split::Train, c);
            const double mean = C ? total / static_cast<double>(C) : 0.0;
            ind.mean_steps = C ? static_cast<double>(runner.steps()) / static_cast<double>(C) : 0.0;
            ind.fitness = mean - cfg_.length_cost * static_cast<double>(ind.program.size()) - cfg_.time_cost * ind.mean_steps;
            ind.evaluated = true;
        });
    };
    auto count_new = [](const std::vector<Individual>& pop) {
        long k = 0;
        for (const auto& ind : pop) k += !ind.evaluated;
        return k;
    };

    std::vector<Individual> pop(P);
    for (auto& ind : pop) ind.program = random_program(spec_, cfg_.variation, rng);

    std::vector<double> eps(C), column(P);
    std::vector<std::size_t> order(C), cand, keep;
    std::iota(order.begin(), order.end(), 0);

    auto select = [&](const std::vector<Individual>& pop) -> std::size_t {
        if (cfg_.selection == SelectionMethod::Tournament || C == 0) {
            std::size_t best = rng.below(pop.size());
            for (int k = 1; k < cfg_.tournament_size; ++k) {
                std::size_t j = rng.below(pop.size());
                if (pop[j].fitness > pop[best].fitness) best = j;
            }
            return best;
        }
        // epsilon-lexicase: filter by randomly ordered cases, keeping the near-best on each
        for (std::size_t i = C; i > 1; --i) std::swap(order[i - 1], order[rng.below(i)]);
        cand.resize(pop.size());
        std::iota(cand.begin(), cand.end(), 0);
        for (std::size_t c : order) {
            double best = -1e300;
            for (std::size_t i : cand) best = std::max(best, pop[i].scores[c]);
            keep.clear();
            for (std::size_t i : cand)
                if (pop[i].scores[c] >= best - eps[c]) keep.push_back(i);
            cand.swap(keep);
            if (cand.size() <= 1) break;
        }
        return cand[rng.below(cand.size())];
    };

    EvolutionResult result;
    int gen = 0;
    for (;; ++gen) {
        evaluations += count_new(pop);
        evaluate(pop);

        std::size_t best = 0;
        double sum_fit = 0.0, sum_len = 0.0;
        for (std::size_t i = 0; i < pop.size(); ++i) {
            if (pop[i].fitness > pop[best].fitness) best = i;
            sum_fit += pop[i].fitness;
            sum_len += static_cast<double>(pop[i].program.size());
        }
        const Program& bp = pop[best].program;

        GenerationReport rep;
        rep.generation = gen;
        rep.program_evaluations = evaluations;
        rep.seconds = elapsed();
        rep.best_fitness = pop[best].fitness;
        rep.mean_fitness = sum_fit / static_cast<double>(pop.size());
        rep.best_length = static_cast<double>(bp.size());
        rep.best_effective_length = static_cast<double>(effective_length(spec_, bp, outputs));
        rep.mean_length = sum_len / static_cast<double>(pop.size());
        rep.best_train_metrics = problem_.metrics(spec_, bp, Split::Train);
        if (callback_) callback_(rep);

        result.best = bp;
        result.best_fitness = pop[best].fitness;
        const bool solved = cfg_.stop_when_solved && problem_.solved(spec_, bp, Split::Train);
        const bool budget = cfg_.max_program_evaluations > 0 &&
                            evaluations + static_cast<long>(P) - cfg_.elitism > cfg_.max_program_evaluations;
        if (solved || budget || gen + 1 >= cfg_.generations) break;

        // epsilon for lexicase = median absolute deviation of each case's scores
        if (cfg_.selection == SelectionMethod::Lexicase) {
            for (std::size_t c = 0; c < C; ++c) {
                for (std::size_t i = 0; i < P; ++i) column[i] = pop[i].scores[c];
                const double med = median_of(column);
                for (std::size_t i = 0; i < P; ++i) column[i] = std::fabs(pop[i].scores[c] - med);
                eps[c] = median_of(column);
            }
        }

        std::vector<Individual> next;
        next.reserve(P);
        std::vector<std::size_t> ranked(P);
        std::iota(ranked.begin(), ranked.end(), 0);
        const std::size_t elites = std::min<std::size_t>(static_cast<std::size_t>(std::max(0, cfg_.elitism)), P);
        std::partial_sort(ranked.begin(), ranked.begin() + static_cast<long>(elites), ranked.end(),
                          [&](std::size_t x, std::size_t y) { return pop[x].fitness > pop[y].fitness; });
        for (std::size_t k = 0; k < elites; ++k) next.push_back(pop[ranked[k]]);

        while (next.size() < P) {
            Individual child;
            const Program& mother = pop[select(pop)].program;
            if (rng.chance(cfg_.variation.p_crossover))
                child.program = crossover(mother, pop[select(pop)].program, cfg_.variation, rng);
            else
                child.program = mother;
            if (rng.chance(cfg_.variation.p_mutation)) mutate(child.program, spec_, cfg_.variation, rng, outputs);
            next.push_back(std::move(child));
        }
        pop.swap(next);
    }

    result.generations = gen + 1;
    result.program_evaluations = evaluations;
    result.train_metrics = problem_.metrics(spec_, result.best, Split::Train);
    result.test_metrics = problem_.metrics(spec_, result.best, Split::Test);
    result.solved_train = problem_.solved(spec_, result.best, Split::Train);
    result.solved_test = problem_.solved(spec_, result.best, Split::Test);
    result.seconds = elapsed();
    return result;
}

}  // namespace adaptogen
