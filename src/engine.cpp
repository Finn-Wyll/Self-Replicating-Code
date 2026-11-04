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
    std::size_t mother = static_cast<std::size_t>(-1);  // index of the mother in the previous population
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


    long evaluations = 0, case_evaluations = 0, cache_hits = 0, tuned = 0;
    long transfers = 0, duplications = 0;

    // Cases scored this generation: positions into fit_cases. All of them unless down-sampling.
    // (The curriculum ranks every case each generation, so it always uses all of them.)
    const std::size_t sample =
        std::max<std::size_t>({1, static_cast<std::size_t>(std::ceil(cfg_.downsample * static_cast<double>(C))),
                               std::min<std::size_t>(C, static_cast<std::size_t>(std::max(1, cfg_.downsample_min_cases)))});
    const bool downsampling = !curriculum && cfg_.downsample < 1.0 && sample < C;
    const std::size_t S = downsampling ? sample : C;
    std::vector<std::size_t> active(C);
    std::iota(active.begin(), active.end(), 0);
    auto resample = [&] {
        if (!downsampling) return;
        std::iota(active.begin(), active.end(), 0);
        for (std::size_t i = 0; i < S; ++i) std::swap(active[i], active[i + rng.below(C - i)]);  // partial shuffle
    };
    resample();

    auto score_on_active = [&](Individual& ind) {
        Runner runner(spec_);
        ind.scores.resize(S);
        double total = 0.0;
        for (std::size_t k = 0; k < S; ++k)
            total += ind.scores[k] = problem_.evaluate_case(runner, ind.program, Split::Train, fit_cases[active[k]]);
        const double mean = S ? total / static_cast<double>(S) : 0.0;
        ind.mean_steps = S ? static_cast<double>(runner.steps()) / static_cast<double>(S) : 0.0;
        ind.fitness = mean - cfg_.length_cost * static_cast<double>(ind.program.size()) - cfg_.time_cost * ind.mean_steps;
    };

    auto evaluate = [&](std::vector<Individual>& pop) {
        long fresh = 0;
        for (const auto& ind : pop) fresh += !ind.evaluated;
        evaluations += fresh;
        case_evaluations += fresh * static_cast<long>(S);
        parallel_for(pop.size(), cfg_.threads, [&](std::size_t i) {
            Individual& ind = pop[i];
            if (ind.evaluated) return;
            score_on_active(ind);
            ind.stagnation = ind.fitness > ind.parent_fitness + 1e-12 ? 0 : ind.parent_stagnation + 1;
            ind.evaluated = true;
        });
    };

    // Fitness of a program on all selection cases (used for the champion when down-sampling).
    auto full_fitness = [&](const Program& p) {
        Runner runner(spec_);
        double total = 0;
        for (std::size_t c : fit_cases) total += problem_.evaluate_case(runner, p, Split::Train, c);
        case_evaluations += static_cast<long>(C);
        const double n = static_cast<double>(std::max<std::size_t>(1, C));
        return total / n - cfg_.length_cost * static_cast<double>(p.size()) - cfg_.time_cost * static_cast<double>(runner.steps()) / n;
    };

    std::vector<Individual> pop(P);
    const std::size_t n_seeded =
        seeds_.empty() ? 0
                       : std::min(P, static_cast<std::size_t>(std::lround((1.0 - std::clamp(seed_random_fraction_, 0.0, 1.0)) *
                                                                          static_cast<double>(P))));
    for (std::size_t i = 0; i < P; ++i) {
        Individual& ind = pop[i];
        if (i < n_seeded) {
            ind.program = seeds_[i % seeds_.size()];
            if (i >= seeds_.size()) mutate(ind.program, spec_, var, rng, outputs);
            if (!mech.hypermutation.enabled) ind.program.mutation_rate = mech.hypermutation.initial_rate;
        } else {
            ind.program = random_program(spec_, var, rng);
            ind.program.mutation_rate = mech.hypermutation.initial_rate;
        }
    }

    // ---- selection helpers -------------------------------------------------------------
    std::vector<double> eps(S), column(P);
    std::vector<double> matrix;  // case-major copy of the scores: matrix[k * P + i] (cache-friendly lexicase)
    auto build_matrix = [&](const std::vector<Individual>& pop) {
        if (cfg_.selection != SelectionMethod::Lexicase) return;
        matrix.resize(S * P);
        for (std::size_t i = 0; i < P; ++i)
            for (std::size_t k = 0; k < S; ++k) matrix[k * P + i] = pop[i].scores[k];
    };
    std::vector<std::size_t> case_rank(S), cand, keep, order;
    std::iota(case_rank.begin(), case_rank.end(), 0);  // easiest first (curriculum)
    std::vector<std::size_t> band_cases_count(static_cast<std::size_t>(bands), S);

    auto band_of = [&](std::size_t cell) {
        if (!curriculum) return bands - 1;
        const int x = static_cast<int>(cell % static_cast<std::size_t>(W));
        return std::min(bands - 1, x * bands / W);
    };
    // Fitness as judged in a band: mean over the band's active cases (all cases in the last band).
    auto band_fitness = [&](const Individual& ind, int band) {
        const std::size_t n = band_cases_count[static_cast<std::size_t>(band)];
        if (n >= S) return ind.fitness;
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
            const double* col = matrix.data() + c * P;
            double best = -1e300;
            for (std::size_t i : cand) best = std::max(best, col[i]);
            keep.clear();
            const double cut = best - eps[c];
            for (std::size_t i : cand)
                if (col[i] >= cut) keep.push_back(i);
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
        child.mother = mother;
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
    auto validation_score = [&](const Program& p) {
        Runner runner(spec_);
        double total = 0;
        for (std::size_t c : val_cases) total += problem_.evaluate_case(runner, p, Split::Train, c);
        const double n = static_cast<double>(val_cases.size());
        return total / n - cfg_.length_cost * static_cast<double>(p.size()) -
               cfg_.time_cost * static_cast<double>(runner.steps()) / n;
    };
    // Lamarckian constant tuning: (1+1) search on the effective constants of the best programs,
    // judged on this generation's cases; improvements are written back into the population.
    auto tune_constants = [&](std::vector<Individual>& pop) {
        const auto& ct = cfg_.constant_tuning;
        if (!ct.enabled || ct.top <= 0 || ct.steps <= 0) return;
        std::vector<std::size_t> ranked(pop.size());
        std::iota(ranked.begin(), ranked.end(), 0);
        const std::size_t top = std::min<std::size_t>(static_cast<std::size_t>(ct.top), pop.size());
        std::partial_sort(ranked.begin(), ranked.begin() + static_cast<long>(top), ranked.end(),
                          [&](std::size_t x, std::size_t y) { return pop[x].fitness > pop[y].fitness; });
        for (std::size_t r = 0; r < top; ++r) {
            Individual& ind = pop[ranked[r]];
            const auto mask = effective_mask(spec_, ind.program, outputs);
            std::vector<std::size_t> consts;
            for (std::size_t i = 0; i < ind.program.code.size(); ++i)
                if (mask[i] && ind.program.code[i].op == Op::Const) consts.push_back(i);
            if (consts.empty()) continue;
            for (int step = 0; step < ct.steps; ++step) {
                Individual trial = ind;
                bool changed = false;
                for (std::size_t i : consts)
                    if (consts.size() == 1 || rng.chance(1.0 / static_cast<double>(consts.size()))) {
                        double& v = trial.program.code[i].imm;
                        v += rng.normal() * ct.sigma * std::max(std::fabs(v), 0.1);
                        changed = true;
                    }
                if (!changed) {
                    double& v = trial.program.code[consts[rng.below(consts.size())]].imm;
                    v += rng.normal() * ct.sigma * std::max(std::fabs(v), 0.1);
                }
                score_on_active(trial);
                ++evaluations;
                case_evaluations += static_cast<long>(S);
                if (trial.fitness > ind.fitness + 1e-12) {
                    trial.stagnation = 0;
                    ind = std::move(trial);
                    ++tuned;
                }
            }
        }
    };

    // Offspring identical to their mother reuse her scores (only valid without down-sampling).
    auto reuse_scores = [&](const std::vector<Individual>& parents, std::vector<Individual>& children) {
        if (!cfg_.cache_duplicates) return;
        for (auto& ch : children) {
            if (ch.evaluated || ch.mother >= parents.size()) continue;
            const Individual& m = parents[ch.mother];
            if (!m.evaluated || m.program.code != ch.program.code) continue;
            ch.scores = m.scores;
            ch.fitness = m.fitness;
            ch.mean_steps = m.mean_steps;
            ch.stagnation = ch.parent_stagnation + 1;
            ch.evaluated = true;
            ++cache_hits;
        }
    };

    int gen = 0;
    evaluate(pop);
    for (;; ++gen) {
        tune_constants(pop);
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
        // Champion. With down-sampling the generation's best is re-judged on all cases so that
        // champions from different generations are compared fairly.
        Individual candidate = pop[best];
        if (downsampling) candidate.fitness = full_fitness(candidate.program);
        bool improved = false;
        if (validating) {
            // The final program is chosen on held-back cases, so memorising the selection
            // cases does not win. Ties go to the better training fitness.
            const double v = validation_score(candidate.program);
            case_evaluations += static_cast<long>(val_cases.size());
            if (!have_best || v > hof_validation + 1e-12 ||
                (v >= hof_validation - 1e-12 && candidate.fitness > hall_of_fame.fitness + 1e-12)) {
                hall_of_fame = std::move(candidate);
                hof_validation = v;
                have_best = improved = true;
            }
        } else if (!have_best || candidate.fitness > hall_of_fame.fitness + 1e-12) {
            hall_of_fame = std::move(candidate);
            have_best = improved = true;
        }
        if (improved) population_stagnation = 0;
        else ++population_stagnation;
        const Program& bp = hall_of_fame.program;
        const double Pd = static_cast<double>(pop.size());

        GenerationReport rep;
        rep.generation = gen;
        rep.program_evaluations = evaluations;
        rep.case_evaluations = case_evaluations;
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
                                {"population_stagnation", static_cast<double>(population_stagnation)},
                                {"cache_hits", static_cast<double>(cache_hits)},
                                {"constants_tuned", static_cast<double>(tuned)}};
        if (validating) rep.population_stats.push_back({"best_validation_score", hof_validation});
        transfers = duplications = cache_hits = tuned = 0;
        if (callback_) callback_(rep);

        const bool solved = cfg_.stop_when_solved && problem_.solved(spec_, bp, Split::Train);
        const long next_cost = static_cast<long>(P * S) * (spatial && downsampling ? 2 : 1);
        const bool budget = (cfg_.max_program_evaluations > 0 &&
                             evaluations + static_cast<long>(P) > cfg_.max_program_evaluations) ||
                            (cfg_.max_case_evaluations > 0 && case_evaluations + next_cost > cfg_.max_case_evaluations);
        if (solved || budget || gen + 1 >= cfg_.generations) break;

        // epsilon for lexicase = median absolute deviation of each case's scores
        build_matrix(pop);
        if (cfg_.selection == SelectionMethod::Lexicase) {
            for (std::size_t c = 0; c < S; ++c) {
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
            if (downsampling) {
                resample();
                for (auto& ind : next) ind.evaluated = false;  // scores are tied to the old sample
            } else {
                reuse_scores(pop, next);
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
            if (downsampling) {
                resample();
                for (auto& ind : pop) ind.evaluated = false;  // compare parent and child on the same cases
                evaluate(pop);
            } else {
                reuse_scores(pop, next);
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
    result.case_evaluations = case_evaluations;
    result.train_metrics = problem_.metrics(spec_, result.best, Split::Train);
    result.test_metrics = problem_.metrics(spec_, result.best, Split::Test);
    result.solved_train = problem_.solved(spec_, result.best, Split::Train);
    result.solved_test = problem_.solved(spec_, result.best, Split::Test);
    result.seconds = elapsed();
    return result;
}

}  // namespace adaptogen
