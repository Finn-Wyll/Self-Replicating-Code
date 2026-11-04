// Compare adaptogen with established algorithms, and measure what each bacteria-inspired
// mechanism contributes (ablation), on formula and list problems.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "adaptogen/engine.hpp"
#include "adaptogen/problems/formula.hpp"
#include "adaptogen/problems/lists.hpp"
#include "adaptogen/serialize.hpp"
#include "baselines.hpp"

using namespace adaptogen;
using namespace adaptogen::bench;

namespace {

struct Benchmark {
    std::string name, description, primary;  // primary = name of the main test metric
    std::function<std::unique_ptr<Problem>(std::uint64_t seed)> make;
    bool formula = false;
    double validation_fraction = 0.0;  // held-back training cases for choosing the final program
};

struct Algorithm {
    std::string name, description;
    std::function<AlgoResult(const Problem&, const EngineConfig&)> run;
    std::function<void(EngineConfig&)> configure = [](EngineConfig&) {};
    bool formula_only = false;
};

// Quote a CSV field if it contains a comma, quote or newline (RFC 4180).
std::string csv_field(const std::string& s) {
    if (s.find_first_of(",\"\n") == std::string::npos) return s;
    std::string out = "\"";
    for (char c : s) out += c == '"' ? std::string("\"\"") : std::string(1, c);
    return out + "\"";
}

double median(std::vector<double> v) {
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    const std::size_t n = v.size();
    return n % 2 ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}
double quantile(std::vector<double> v, double q) {
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    return v[static_cast<std::size_t>(q * static_cast<double>(v.size() - 1) + 0.5)];
}

// Two-sided Fisher exact test for a 2x2 table (successes a of n1 vs b of n2).
double fisher_exact(int a, int n1, int b, int n2) {
    const int k = a + b, n = n1 + n2;
    auto lf = [](int x) { return std::lgamma(static_cast<double>(x) + 1.0); };
    auto logp = [&](int x) { return lf(k) + lf(n - k) + lf(n1) + lf(n2) - lf(x) - lf(k - x) - lf(n1 - x) - lf(n2 - k + x) - lf(n); };
    const double observed = logp(a);
    double p = 0;
    for (int x = std::max(0, k - n2); x <= std::min(k, n1); ++x) {
        const double lp = logp(x);
        if (lp <= observed + 1e-9) p += std::exp(lp);
    }
    return std::min(1.0, p);
}

AlgoResult tree_gp(const Problem& p, const EngineConfig& c) { return run_tree_gp(dynamic_cast<const FormulaProblem&>(p), c); }
AlgoResult polyfit(const Problem& p, const EngineConfig& c) { return run_polyfit(dynamic_cast<const FormulaProblem&>(p), c); }

std::vector<Algorithm> baseline_suite() {
    return {
        {"adaptogen", "linear programs, full core instruction set (incl. if/loop), lexicase", run_adaptogen},
        {"adaptogen-classic", "as above, restricted to the tree-GP function set", run_adaptogen,
         [](EngineConfig& c) { c.ops = classic_regression_ops(); }, true},
        {"adaptogen-tournament", "full instruction set, tournament selection (size 4)", run_adaptogen,
         [](EngineConfig& c) { c.selection = SelectionMethod::Tournament; }},
        {"tree-gp", "Koza-style tree GP, {+,-,*,/,sin,cos,exp,log}, tournament 7", tree_gp, [](EngineConfig&) {}, true},
        {"hill-climber", "(1+1) mutation-only search on linear programs, restarts", run_hill_climber},
        {"random-search", "random linear programs, keep the best", run_random_search},
        {"polyfit", "polynomial least squares, degree by hold-out (not evolutionary)", polyfit, [](EngineConfig&) {}, true},
    };
}

std::vector<Algorithm> mechanism_suite() {
    auto with = [](std::function<void(Mechanisms&)> f) {
        return [f](EngineConfig& c) { f(c.mechanisms); };
    };
    auto all = [](Mechanisms& m) {
        m.stress.enabled = m.hypermutation.enabled = m.transfer.enabled = m.duplication.enabled = true;
        m.spatial.enabled = m.curriculum.enabled = true;
    };
    return {
        {"baseline", "stage-1 engine: no mechanisms (crossover + mutation, lexicase)", run_adaptogen},
        {"+stress", "stress-induced mutagenesis in stagnating lineages", run_adaptogen,
         with([](Mechanisms& m) { m.stress.enabled = true; })},
        {"+hypermutation", "heritable, evolving mutation rates", run_adaptogen,
         with([](Mechanisms& m) { m.hypermutation.enabled = true; })},
        {"+transfer", "horizontal transfer of code segments", run_adaptogen,
         with([](Mechanisms& m) { m.transfer.enabled = true; })},
        {"+duplication", "gene duplication", run_adaptogen, with([](Mechanisms& m) { m.duplication.enabled = true; })},
        {"+spatial", "population on a torus with local competition", run_adaptogen,
         with([](Mechanisms& m) { m.spatial.enabled = true; })},
        {"+spatial+curriculum", "spatial + MEGA-plate difficulty bands", run_adaptogen,
         with([](Mechanisms& m) { m.spatial.enabled = m.curriculum.enabled = true; })},
        {"all", "all six mechanisms, crossover kept", run_adaptogen, with(all)},
        {"bacterial", "all six mechanisms, no crossover (asexual + horizontal transfer, like real bacteria)",
         run_adaptogen,
         [all](EngineConfig& c) {
             all(c.mechanisms);
             c.variation.p_crossover = 0.0;
         }},
        {"tree-gp", "reference: Koza-style tree GP", tree_gp, [](EngineConfig&) {}, true},
    };
}

std::vector<Benchmark> all_benchmarks() {
    std::vector<Benchmark> b;
    for (const auto& t : formula_targets()) {
        const std::string name = t.name;
        b.push_back({name, t.formula, "R^2",
                     [name](std::uint64_t seed) -> std::unique_ptr<Problem> {
                         return FormulaProblem::from_params({{"target", name}}, seed);
                     },
                     true});
    }
    for (const char* task : {"sum", "max", "count_positive"}) {
        const std::string t = task;
        b.push_back({"list-" + t, t + " of a list (train length <= 20, test length <= 50)", "accuracy",
                     [t](std::uint64_t seed) -> std::unique_ptr<Problem> {
                         return ListProblem::from_params({{"task", t}}, seed);
                     },
                     false, 0.25});
    }
    return b;
}

}  // namespace

int main(int argc, char** argv) {
    int runs = 20, population = 500, generations = 100, threads = 0;
    std::uint64_t seed_base = 1000;
    int case_budget_gens = 0;  // >0: budget in case evaluations = population x N x training cases
    bool validation = true, reference = false;
    std::vector<std::string> targets, only;
    std::vector<std::pair<std::string, nlohmann::json>> variants;
    nlohmann::json common = nlohmann::json::object();
    std::string suite = "baselines", out_prefix;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) throw std::runtime_error(a + " needs a value");
            return argv[++i];
        };
        if (a == "--runs") runs = std::stoi(next());
        else if (a == "--population") population = std::stoi(next());
        else if (a == "--generations") generations = std::stoi(next());
        else if (a == "--threads") threads = std::stoi(next());
        else if (a == "--target") targets.push_back(next());
        else if (a == "--algorithm") only.push_back(next());
        else if (a == "--suite") suite = next();
        else if (a == "--seed-base") seed_base = std::stoull(next());
        else if (a == "--case-budget-gens") case_budget_gens = std::stoi(next());
        else if (a == "--no-validation") validation = false;
        else if (a == "--reference") reference = true;  // add tree GP (formula benchmarks) as a reference
        else if (a == "--set") {  // config overlay applied to every adaptogen run, e.g. --set '{"length_cost":0.01}'
            common.update(nlohmann::json::parse(next()));
        } else if (a == "--variant") {  // NAME=JSON config overlay, e.g. stress10={"mechanisms":{"stress":{"enabled":true}}}
            const std::string v = next();
            const auto eq = v.find('=');
            if (eq == std::string::npos) throw std::runtime_error("--variant expects NAME=JSON");
            variants.emplace_back(v.substr(0, eq), nlohmann::json::parse(v.substr(eq + 1)));
        }
        else if (a == "--out") out_prefix = next();
        else {
            std::fprintf(stderr,
                         "usage: adaptogen-compare [--suite baselines|mechanisms] [--runs N] [--population N]\n"
                         "         [--generations N] [--threads N] [--target NAME]... [--algorithm NAME]... [--out PREFIX]\n"
                         "         [--seed-base N] [--no-validation] [--set JSON] [--variant NAME=JSON]...\n"
                         "         [--case-budget-gens N]  (budget = population x N x training cases, in case evaluations)\n"
                         "         [--reference]  (custom suite: add tree GP on formula benchmarks)\n"
                         "suites: baselines, mechanisms, custom (baseline + --variant entries)\n"
                         "targets: koza1 nguyen5 nguyen7 nguyen10 pagie1 keijzer6 list-sum list-max list-count_positive\n");
            return 1;
        }
    }
    std::vector<Algorithm> algos;
    if (suite == "baselines") algos = baseline_suite();
    else if (suite == "mechanisms") algos = mechanism_suite();
    else if (suite == "custom") algos = {mechanism_suite().front()};
    else {
        std::fprintf(stderr, "unknown suite '%s'\n", suite.c_str());
        return 1;
    }
    if (reference && suite == "custom") algos.push_back(mechanism_suite().back());  // tree-gp
    for (const auto& [name, overlay] : variants) {
        const nlohmann::json o = overlay;
        algos.push_back({name, "config overlay: " + o.dump(), run_adaptogen,
                         [o](EngineConfig& c) { config_from_json(c, o); }});
    }
    if (out_prefix.empty()) out_prefix = "benchmarks/" + suite;
    if (!only.empty())
        std::erase_if(algos, [&](const Algorithm& a) { return std::find(only.begin(), only.end(), a.name) == only.end(); });

    std::vector<Benchmark> benches = all_benchmarks();
    if (!targets.empty())
        std::erase_if(benches, [&](const Benchmark& b) { return std::find(targets.begin(), targets.end(), b.name) == targets.end(); });
    else if (suite == "baselines")
        std::erase_if(benches, [](const Benchmark& b) { return !b.formula; });

    std::ofstream raw(out_prefix + ".csv");
    raw << "benchmark,algorithm,run,seed,solved_test,test_primary,program_evaluations,case_evaluations,seconds,size\n";
    std::ofstream md(out_prefix + ".md");
    md << "# Results: " << suite << "\n\n"
       << "Budget per run: population " << population << " x " << generations << " generations = "
       << static_cast<long>(population) * generations << " program evaluations. " << runs
       << " independent runs per cell; each run samples its own data. \"Solved\" is judged on the **unseen test "
       << "set** (formulas: RMSE < 1% of the target's spread; lists: every test list answered exactly). "
       << "Runs stop early when the training set is solved. p = two-sided Fisher exact test of the solved "
       << "count against the first algorithm in the table. Seeds start at " << seed_base << "."
       << (case_budget_gens > 0 ? " Budget: " + std::to_string(case_budget_gens) +
                                      " generation-equivalents in **case evaluations** (population x N x training cases); "
                                      "runs may use them for more generations on fewer cases."
                                : std::string())
       << (validation ? " List benchmarks hold back 25% of the training cases to choose the final program." : "")
       << (common.empty() ? std::string() : " Common config overlay: `" + common.dump() + "`.") << "\n\n### Algorithms\n\n";
    for (const auto& a : algos) md << "- **" << a.name << "**: " << a.description << "\n";
    md << "\n";

    std::mutex mu;
    for (const auto& bench : benches) {
        std::printf("\n== %s: %s\n", bench.name.c_str(), bench.description.c_str());
        std::printf("%-22s %8s %7s %12s %18s %10s %8s %6s\n", "algorithm", "solved", "p", ("median " + bench.primary).c_str(),
                    "25%..75%", "evals", "time", "size");
        md << "## " << bench.name << ": " << bench.description << "\n\n"
           << "| algorithm | solved (test) | p vs first | median test " << bench.primary << " | 25%..75% | median evaluations | median case evaluations | median time (s) | median size |\n"
           << "|---|---|---|---|---|---|---|---|---|\n";
        int first_solved = -1;
        for (const auto& algo : algos) {
            if (algo.formula_only && !bench.formula) continue;
            std::vector<AlgoResult> results(static_cast<std::size_t>(runs));
            parallel_for(static_cast<std::size_t>(runs), threads, [&](std::size_t r) {
                EngineConfig cfg;
                cfg.seed = seed_base + r;
                cfg.population = population;
                cfg.generations = generations;
                cfg.threads = 1;  // parallelism is across runs
                // Experiments are defined in "generations of full evaluation" (the engine's default
                // before down-sampling became the default), so published results stay reproducible.
                // Variants that want down-sampling set it explicitly, usually with --case-budget-gens.
                cfg.downsample = 1.0;
                if (validation) cfg.validation_fraction = bench.validation_fraction;
                config_from_json(cfg, common);
                algo.configure(cfg);
                auto problem = bench.make(cfg.seed);
                if (case_budget_gens > 0 && !algo.formula_only) {  // baselines without a case budget keep N generations
                    cfg.max_case_evaluations = static_cast<long>(population) * case_budget_gens *
                                               static_cast<long>(problem->num_cases(Split::Train));
                    cfg.generations = 1000000;  // the case budget decides when to stop
                } else if (case_budget_gens > 0) {
                    cfg.generations = case_budget_gens;  // tree GP evaluates every case: same budget
                }
                results[r] = algo.run(*problem, cfg);
                std::lock_guard<std::mutex> lock(mu);
                raw << csv_field(bench.name) << ',' << csv_field(algo.name) << ',' << r << ',' << cfg.seed << ',' << results[r].solved_test << ','
                    << results[r].test_primary << ',' << results[r].program_evaluations << ','
                    << results[r].case_evaluations << ',' << results[r].seconds << ','
                    << results[r].size << '\n';
            });
            std::vector<double> prim, ev, cev, sec, size;
            int solved = 0;
            for (const auto& r : results) {
                solved += r.solved_test;
                prim.push_back(r.test_primary);
                ev.push_back(static_cast<double>(r.program_evaluations));
                cev.push_back(static_cast<double>(r.case_evaluations));
                sec.push_back(r.seconds);
                size.push_back(r.size);
            }
            std::string p = "-";
            if (first_solved < 0) first_solved = solved;
            else {
                char buf[16];
                std::snprintf(buf, sizeof buf, "%.3f", fisher_exact(solved, runs, first_solved, runs));
                p = buf;
            }
            const double lo = quantile(prim, 0.25), hi = quantile(prim, 0.75), med = median(prim);
            char line[320];
            std::snprintf(line, sizeof line, "%-22s %5d/%-2d %7s %12.5g %8.4g..%-8.4g %10.0f %7.2fs %6.0f", algo.name.c_str(),
                          solved, runs, p.c_str(), med, lo, hi, median(ev), median(sec), median(size));
            std::puts(line);
            std::fflush(stdout);
            std::snprintf(line, sizeof line, "| %s | %d/%d | %s | %.5g | %.4g .. %.4g | %.0f | %.0f | %.2f | %.0f |\n",
                          algo.name.c_str(), solved, runs, p.c_str(), med, lo, hi, median(ev), median(cev), median(sec),
                          median(size));
            md << line;
        }
        md << "\n";
    }
    std::printf("\nwrote %s.md and %s.csv\n", out_prefix.c_str(), out_prefix.c_str());
    return 0;
}
