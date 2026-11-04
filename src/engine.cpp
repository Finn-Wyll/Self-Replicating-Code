#include "adaptogen/engine.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
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
    // lineage state for the stress response
    double parent_fitness = -1e300;
    int parent_stagnation = 0;
    int stagnation = 0;
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

    const Mechanisms& mech = cfg_.mechanisms;
    const VariationConfig& var = cfg_.variation;
    Rng rng(cfg_.seed);
    const int outputs = problem_.num_outputs();
    // Training cases used for selection ("fit") and, optionally, held-back validation cases
    // used only to choose the final program.
    std::vector<std::size_t> fit_cases(problem_.num_cases(Split::Train)), val_cases;
    std::iota(fit_cases.begin(), fit_cases.end(), 0);
    if (cfg_.validation_fraction > 0 && fit_cases.size() >= 4) {
        Rng split_rng(cfg_.seed ^ 0x7a11da7eULL);
        for (std::size_t i = fit_cases.size(); i > 1; --i) std::swap(fit_cases[i - 1], fit_cases[split_rng.below(i)]);
        const std::size_t nv = std::min(fit_cases.size() - 2,
                                        static_cast<std::size_t>(std::clamp(cfg_.validation_fraction, 0.0, 0.9) *
                                                                 static_cast<double>(fit_cases.size())));
        val_cases.assign(fit_cases.end() - static_cast<long>(nv), fit_cases.end());
        fit_cases.resize(fit_cases.size() - nv);
        std::sort(fit_cases.begin(), fit_cases.end());
    }
    const std::size_t C = fit_cases.size();
    const bool validating = !val_cases.empty();

    // Population layout: a plain list, or a W x H torus when spatial structure is on.
    std::size_t P = static_cast<std::size_t>(std::max(2, cfg_.population));
    int W = 0, H = 0;
    const bool spatial = mech.spatial.enabled;
    const bool curriculum = spatial && mech.curriculum.enabled && C > 1;
    if (spatial) {
        W = mech.spatial.width > 0 ? mech.spatial.width
                                   : static_cast<int>(std::ceil(std::sqrt(static_cast<double>(P))));
        H = static_cast<int>((P + static_cast<std::size_t>(W) - 1) / static_cast<std::size_t>(W));
        P = static_cast<std::size_t>(W) * static_cast<std::size_t>(H);
    }
    const int bands = curriculum ? std::clamp(mech.curriculum.bands, 1, std::max(1, W)) : 1;

    long evaluations = 0;
    long transfers = 0, duplications = 0;

    auto evaluate = [&](std::vector<Individual>& pop) {
        long fresh = 0;
        for (const auto& ind : pop) fresh += !ind.evaluated;
        evaluations += fresh;
        parallel_for(pop.size(), cfg_.threads, [&](std::size_t i) {
            Individual& ind = pop[i];
            if (ind.evaluated) return;
            Runner runner(spec_);
            ind.scores.resize(C);
            double total = 0.0;
            for (std::size_t c = 0; c < C; ++c)
                total += ind.scores[c] = problem_.evaluate_case(runner, ind.program, Split::Train, fit_cases[c]);
            const double mean = C ? total / static_cast<double>(C) : 0.0;
            ind.mean_steps = C ? static_cast<double>(runner.steps()) / static_cast<double>(C) : 0.0;
            ind.fitness = mean - cfg_.length_cost * static_cast<double>(ind.program.size()) -
                          cfg_.time_cost * ind.mean_steps;
            ind.stagnation = ind.fitness > ind.parent_fitness + 1e-12 ? 0 : ind.parent_stagnation + 1;
            ind.evaluated = true;
        });
    };

    std::vector<Individual> pop(P);
    for (auto& ind : pop) {
        ind.program = random_program(spec_, var, rng);
        ind.program.mutation_rate = mech.hypermutation.initial_rate;
    }

    // ---- selection helpers -------------------------------------------------------------
    std::vector<double> eps(C), column(P);
    std::vector<std::size_t> all_cases(C), case_rank(C), cand, keep, order;
    std::iota(all_cases.begin(), all_cases.end(), 0);
    std::iota(case_rank.begin(), case_rank.end(), 0);  // easiest first (curriculum)
    std::vector<std::size_t> band_cases_count(static_cast<std::size_t>(bands), C);

    auto band_of = [&](std::size_t cell) {
        if (!curriculum) return bands - 1;
        const int x = static_cast<int>(cell % static_cast<std::size_t>(W));
        return std::min(bands - 1, x * bands / W);
    };
    // Fitness as judged in a band: mean over the band's active cases (all cases in the last band).
    auto band_fitness = [&](const Individual& ind, int band) {
        const std::size_t n = band_cases_count[static_cast<std::size_t>(band)];
        if (n >= C) return ind.fitness;
        double s = 0;
        for (std::size_t k = 0; k < n; ++k) s += ind.scores[case_rank[k]];
        return s / static_cast<double>(n) - cfg_.length_cost * static_cast<double>(ind.program.size()) -
               cfg_.time_cost * ind.mean_steps;
    };

    // Select from `pool` (indices into pop) as judged in `band`.
    auto select = [&](const std::vector<Individual>& pop, const std::vector<std::size_t>* pool, int band) -> std::size_t {
        const std::size_t n = pool ? pool->size() : pop.size();
        auto at = [&](std::size_t k) { return pool ? (*pool)[k] : k; };
        if (cfg_.selection == SelectionMethod::Tournament || C == 0) {
            std::size_t best = at(rng.below(n));
            double bf = band_fitness(pop[best], band);
            for (int k = 1; k < cfg_.tournament_size; ++k) {
                const std::size_t j = at(rng.below(n));
                const double f = band_fitness(pop[j], band);
                if (f > bf) {
                    best = j;
                    bf = f;
                }
            }
            return best;
        }
        // epsilon-lexicase over the band's active cases, in random order
        const std::size_t active = band_cases_count[static_cast<std::size_t>(band)];
        order.assign(case_rank.begin(), case_rank.begin() + static_cast<long>(active));
        for (std::size_t i = order.size(); i > 1; --i) std::swap(order[i - 1], order[rng.below(i)]);
        cand.clear();
        for (std::size_t k = 0; k < n; ++k) cand.push_back(at(k));
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

    // ---- reproduction --------------------------------------------------------------------
    const bool population_stress = mech.stress.trigger == "population";
    int population_stagnation = 0;  // generations since the best program last improved
    auto stress_multiplier = [&](int stagnation) {
        if (!mech.stress.enabled) return 1.0;
        const int over = (population_stress ? population_stagnation : stagnation) - mech.stress.patience;
        return over <= 0 ? 1.0 : std::min(mech.stress.max_multiplier, 1.0 + mech.stress.growth * over);
    };

    // mother/father/donor are indices into pop.
    auto make_child = [&](const std::vector<Individual>& pop, std::size_t mother, std::size_t father, std::size_t donor) {
        Individual child;
        const Individual& m = pop[mother];
        child.parent_fitness = m.fitness;
        child.parent_stagnation = m.stagnation;
        child.program = m.program;
        if (father != mother && rng.chance(var.p_crossover))
            child.program = crossover(m.program, pop[father].program, var, rng);
        if (mech.duplication.enabled && rng.chance(mech.duplication.rate))
            duplications += duplicate_segment(child.program, var, mech.duplication.max_segment, rng);
        if (mech.transfer.enabled && donor != mother && rng.chance(mech.transfer.rate))
            transfers += transfer_segment(child.program, pop[donor].program, var, mech.transfer.max_segment, rng);

        if (!mech.hypermutation.enabled && !mech.stress.enabled) {
            if (rng.chance(var.p_mutation)) mutate(child.program, spec_, var, rng, outputs);
        } else {
            const double base = mech.hypermutation.enabled ? child.program.mutation_rate : var.p_mutation;
            const int k = rng.poisson(base * stress_multiplier(m.stagnation));
            for (int i = 0; i < k; ++i) mutate(child.program, spec_, var, rng, outputs);
        }
        if (mech.hypermutation.enabled && rng.chance(mech.hypermutation.p_change))
            child.program.mutation_rate =
                std::clamp(child.program.mutation_rate * std::exp(rng.normal() * mech.hypermutation.change_sigma),
                           mech.hypermutation.min_rate, mech.hypermutation.max_rate);
        return child;
    };

    std::vector<std::vector<std::size_t>> neighbours;
    if (spatial) {
        neighbours.resize(P);
        const int r = std::max(1, mech.spatial.radius);
        for (std::size_t cell = 0; cell < P; ++cell) {
            const int x = static_cast<int>(cell % static_cast<std::size_t>(W)), y = static_cast<int>(cell / static_cast<std::size_t>(W));
            for (int dy = -r; dy <= r; ++dy)
                for (int dx = -r; dx <= r; ++dx) {
                    const int nx = ((x + dx) % W + W) % W, ny = ((y + dy) % H + H) % H;
                    neighbours[cell].push_back(static_cast<std::size_t>(ny) * static_cast<std::size_t>(W) + static_cast<std::size_t>(nx));
                }
        }
    }

    // ---- main loop -----------------------------------------------------------------------
    EvolutionResult result;
    Individual hall_of_fame;  // best ever (spatial replacement can lose the global best)
    bool have_best = false;
    double hof_validation = -1e300;
    double last_best_fitness = -1e300;
    auto validation_score = [&](const Program& p) {
        Runner runner(spec_);
        double total = 0;
        for (std::size_t c : val_cases) total += problem_.evaluate_case(runner, p, Split::Train, c);
        const double n = static_cast<double>(val_cases.size());
        return total / n - cfg_.length_cost * static_cast<double>(p.size()) -
               cfg_.time_cost * static_cast<double>(runner.steps()) / n;
    };
    int gen = 0;
    evaluate(pop);
    for (;; ++gen) {
        std::size_t best = 0;
        double sum_fit = 0, sum_len = 0, sum_rate = 0, sum_stress = 0;
        int max_stag = 0;
        for (std::size_t i = 0; i < pop.size(); ++i) {
            if (pop[i].fitness > pop[best].fitness) best = i;
            sum_fit += pop[i].fitness;
            sum_len += static_cast<double>(pop[i].program.size());
            sum_rate += pop[i].program.mutation_rate;
            sum_stress += stress_multiplier(pop[i].stagnation);
            max_stag = std::max(max_stag, pop[i].stagnation);
        }
        if (validating) {
            // The final program is chosen on held-back cases, so memorising the selection
            // cases does not win. Ties go to the better training fitness.
            const double v = validation_score(pop[best].program);
            if (!have_best || v > hof_validation + 1e-12 ||
                (v >= hof_validation - 1e-12 && pop[best].fitness > hall_of_fame.fitness)) {
                hall_of_fame = pop[best];
                hof_validation = v;
                have_best = true;
            }
        } else if (!have_best || pop[best].fitness > hall_of_fame.fitness) {
            hall_of_fame = pop[best];
            have_best = true;
        }
        if (pop[best].fitness > last_best_fitness + 1e-12) {
            last_best_fitness = pop[best].fitness;
            population_stagnation = 0;
        } else {
            ++population_stagnation;
        }
        const Program& bp = hall_of_fame.program;
        const double Pd = static_cast<double>(pop.size());

        GenerationReport rep;
        rep.generation = gen;
        rep.program_evaluations = evaluations;
        rep.seconds = elapsed();
        rep.best_fitness = hall_of_fame.fitness;  // champion's fitness on the selection cases
        rep.mean_fitness = sum_fit / Pd;
        rep.best_length = static_cast<double>(bp.size());
        rep.best_effective_length = static_cast<double>(effective_length(spec_, bp, outputs));
        rep.mean_length = sum_len / Pd;
        rep.best_train_metrics = problem_.metrics(spec_, bp, Split::Train);
        rep.population_stats = {{"mean_mutation_rate", mech.hypermutation.enabled ? sum_rate / Pd : var.p_mutation},
                                {"mean_stress_multiplier", sum_stress / Pd},
                                {"max_stagnation", static_cast<double>(max_stag)},
                                {"transfers", static_cast<double>(transfers)},
                                {"duplications", static_cast<double>(duplications)},
                                {"population_stagnation", static_cast<double>(population_stagnation)}};
        if (validating) rep.population_stats.push_back({"best_validation_score", hof_validation});
        transfers = duplications = 0;
        if (callback_) callback_(rep);

        const bool solved = cfg_.stop_when_solved && problem_.solved(spec_, bp, Split::Train);
        const bool budget = cfg_.max_program_evaluations > 0 &&
                            evaluations + static_cast<long>(P) > cfg_.max_program_evaluations;
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
        // curriculum: rank cases from easiest (population does well) to hardest
        if (curriculum && gen % std::max(1, mech.curriculum.refresh) == 0) {
            std::vector<double> mean_score(C, 0.0);
            for (const auto& ind : pop)
                for (std::size_t c = 0; c < C; ++c) mean_score[c] += ind.scores[c];
            std::stable_sort(case_rank.begin(), case_rank.end(),
                             [&](std::size_t a, std::size_t b) { return mean_score[a] > mean_score[b]; });
            for (int b = 0; b < bands; ++b)
                band_cases_count[static_cast<std::size_t>(b)] = std::max<std::size_t>(
                    1, static_cast<std::size_t>(std::ceil(static_cast<double>(C) * (b + 1) / bands)));
        }

        std::vector<Individual> next;
        next.reserve(P);
        if (!spatial) {
            std::vector<std::size_t> ranked(P);
            std::iota(ranked.begin(), ranked.end(), 0);
            const std::size_t elites = std::min<std::size_t>(static_cast<std::size_t>(std::max(0, cfg_.elitism)), P);
            std::partial_sort(ranked.begin(), ranked.begin() + static_cast<long>(elites), ranked.end(),
                              [&](std::size_t x, std::size_t y) { return pop[x].fitness > pop[y].fitness; });
            for (std::size_t k = 0; k < elites; ++k) {
                next.push_back(pop[ranked[k]]);
                next.back().stagnation++;  // survivors that did not reproduce keep ageing
            }
            while (next.size() < P) {
                const std::size_t mother = select(pop, nullptr, bands - 1);
                const std::size_t father = var.p_crossover > 0 ? select(pop, nullptr, bands - 1) : mother;
                next.push_back(make_child(pop, mother, father, rng.below(P)));
            }
            evaluate(next);
            pop.swap(next);
        } else {
            for (std::size_t cell = 0; cell < P; ++cell) {
                const auto& nb = neighbours[cell];
                const int band = band_of(cell);
                const std::size_t mother = select(pop, &nb, band);
                const std::size_t father = var.p_crossover > 0 ? select(pop, &nb, band) : mother;
                next.push_back(make_child(pop, mother, father, nb[rng.below(nb.size())]));
            }
            evaluate(next);
            for (std::size_t cell = 0; cell < P; ++cell) {
                const int band = band_of(cell);
                if (band_fitness(next[cell], band) >= band_fitness(pop[cell], band)) pop[cell] = std::move(next[cell]);
                else pop[cell].stagnation++;
            }
        }
    }

    result.best = hall_of_fame.program;
    result.best_fitness = hall_of_fame.fitness;
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
