// Compare adaptogen with established algorithms on standard formula-discovery benchmarks.
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "adaptogen/engine.hpp"
#include "adaptogen/problems/formula.hpp"
#include "baselines.hpp"

using namespace adaptogen;
using namespace adaptogen::bench;

namespace {

struct Algorithm {
    std::string name;
    std::string description;
    std::function<AlgoResult(const FormulaProblem&, const EngineConfig&)> run;
    std::function<void(EngineConfig&)> configure;
};

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

}  // namespace

int main(int argc, char** argv) {
    int runs = 20, population = 500, generations = 100, threads = 0;
    std::vector<std::string> targets, only;
    std::string out_prefix = "benchmarks/results";
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
        else if (a == "--out") out_prefix = next();
        else {
            std::fprintf(stderr,
                         "usage: adaptogen-compare [--runs N] [--population N] [--generations N] [--threads N]\n"
                         "                         [--target NAME]... [--algorithm NAME]... [--out PREFIX]\n");
            return 1;
        }
    }
    if (targets.empty())
        for (const auto& t : formula_targets()) targets.push_back(t.name);

    std::vector<Algorithm> algos = {
        {"adaptogen", "linear programs, full core instruction set (incl. if/loop), lexicase", run_adaptogen,
         [](EngineConfig&) {}},
        {"adaptogen-classic", "as above, restricted to the tree-GP function set", run_adaptogen,
         [](EngineConfig& c) { c.ops = classic_regression_ops(); }},
        {"adaptogen-tournament", "full instruction set, tournament selection (size 4)", run_adaptogen,
         [](EngineConfig& c) { c.selection = SelectionMethod::Tournament; }},
        {"tree-gp", "Koza-style tree GP, {+,-,*,/,sin,cos,exp,log}, tournament 7", run_tree_gp, [](EngineConfig&) {}},
        {"hill-climber", "(1+1) mutation-only search on linear programs, restarts", run_hill_climber,
         [](EngineConfig&) {}},
        {"random-search", "random linear programs, keep the best", run_random_search, [](EngineConfig&) {}},
        {"polyfit", "polynomial least squares, degree by hold-out (not evolutionary)", run_polyfit,
         [](EngineConfig&) {}},
    };
    if (!only.empty())
        std::erase_if(algos, [&](const Algorithm& a) { return std::find(only.begin(), only.end(), a.name) == only.end(); });

    std::ofstream raw(out_prefix + ".csv");
    raw << "target,algorithm,run,seed,solved_test,test_r2,test_nrmse,train_nrmse,program_evaluations,seconds,size\n";
    std::ofstream md(out_prefix + ".md");
    md << "# Benchmark results\n\n"
       << "Budget per run: population " << population << " x " << generations << " generations = "
       << static_cast<long>(population) * generations << " program evaluations (random search and hill climbing "
       << "get the same number of evaluations). " << runs << " independent runs per cell; each run samples its "
       << "own training data. \"Solved\" = RMSE on the **unseen test set** below 1% of the target's standard "
       << "deviation (R^2 > 0.9999). Runs stop early when the training set is solved.\n\n### Algorithms\n\n";
    for (const auto& a : algos) md << "- **" << a.name << "**: " << a.description << "\n";
    md << "\n";

    std::mutex mu;
    for (const auto& target : targets) {
        const FormulaTarget* t = nullptr;
        for (const auto& c : formula_targets())
            if (c.name == target) t = &c;
        if (!t) {
            std::fprintf(stderr, "unknown target %s\n", target.c_str());
            return 1;
        }
        std::printf("\n== %s: %s\n", t->name.c_str(), t->formula.c_str());
        std::printf("%-22s %8s %10s %16s %12s %9s %8s\n", "algorithm", "solved", "median R2", "R2 (25%..75%)",
                    "evals", "time", "size");
        md << "## " << t->name << ": `" << t->formula << "`\n\n"
           << "| algorithm | solved (test) | median test R^2 | R^2 25%..75% | median evaluations | median time (s) | median size |\n"
           << "|---|---|---|---|---|---|---|\n";
        for (const auto& algo : algos) {
            std::vector<AlgoResult> results(static_cast<std::size_t>(runs));
            parallel_for(static_cast<std::size_t>(runs), threads, [&](std::size_t r) {
                EngineConfig cfg;
                cfg.seed = 1000 + r;
                cfg.population = population;
                cfg.generations = generations;
                cfg.threads = 1;  // parallelism is across runs
                algo.configure(cfg);
                auto problem = FormulaProblem::from_params({{"target", target}}, cfg.seed);
                results[r] = algo.run(*problem, cfg);
                std::lock_guard<std::mutex> lock(mu);
                raw << target << ',' << algo.name << ',' << r << ',' << cfg.seed << ',' << results[r].solved_test
                    << ',' << results[r].test_r2 << ',' << results[r].test_nrmse << ',' << results[r].train_nrmse
                    << ',' << results[r].program_evaluations << ',' << results[r].seconds << ',' << results[r].size
                    << '\n';
            });
            std::vector<double> r2, ev, sec, size;
            int solved = 0;
            for (const auto& r : results) {
                solved += r.solved_test;
                r2.push_back(r.test_r2);
                ev.push_back(static_cast<double>(r.program_evaluations));
                sec.push_back(r.seconds);
                size.push_back(r.size);
            }
            char line[256];
            std::snprintf(line, sizeof line, "%-22s %5d/%-2d %10.5f %7.4f..%-7.4f %12.0f %8.2fs %8.0f", algo.name.c_str(),
                          solved, runs, median(r2), quantile(r2, 0.25), quantile(r2, 0.75), median(ev), median(sec),
                          median(size));
            std::puts(line);
            std::fflush(stdout);
            char row[320];
            std::snprintf(row, sizeof row, "| %s | %d/%d | %.5f | %.4f .. %.4f | %.0f | %.2f | %.0f |\n",
                          algo.name.c_str(), solved, runs, median(r2), quantile(r2, 0.25), quantile(r2, 0.75),
                          median(ev), median(sec), median(size));
            md << row;
        }
        md << "\n";
    }
    std::printf("\nwrote %s.md and %s.csv\n", out_prefix.c_str(), out_prefix.c_str());
    return 0;
}
