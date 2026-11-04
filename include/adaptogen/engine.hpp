#pragma once
#include <functional>
#include <string>
#include <vector>

#include "adaptogen/problem.hpp"
#include "adaptogen/variation.hpp"

namespace adaptogen {

enum class SelectionMethod { Tournament, Lexicase };

struct EngineConfig {
    std::uint64_t seed = 1;
    int population = 500;
    int generations = 100;
    int elitism = 1;
    SelectionMethod selection = SelectionMethod::Lexicase;
    int tournament_size = 4;
    int num_registers = 8;
    std::vector<Op> ops;  // empty = problem's default_ops()
    VmLimits limits;
    VariationConfig variation;
    double const_min = -5.0, const_max = 5.0;
    double length_cost = 0.0;  // fitness penalty per instruction ("cost of resistance")
    double time_cost = 0.0;    // fitness penalty per executed instruction per case
    int threads = 0;           // 0 = hardware concurrency
    bool stop_when_solved = true;
    long max_program_evaluations = 0;  // 0 = no budget beyond generations
};

/// Published every generation (the event API that CLI, MCP and a future GUI consume).
struct GenerationReport {
    int generation = 0;
    long program_evaluations = 0;  // programs evaluated on the full training set so far
    double seconds = 0.0;
    double best_fitness = 0.0;
    double mean_fitness = 0.0;
    double best_length = 0.0;
    double best_effective_length = 0.0;
    double mean_length = 0.0;
    std::vector<Metric> best_train_metrics;
};

struct EvolutionResult {
    Program best;
    double best_fitness = 0.0;
    std::vector<Metric> train_metrics, test_metrics;
    bool solved_train = false, solved_test = false;
    int generations = 0;
    long program_evaluations = 0;
    double seconds = 0.0;
};

/// Build the machine (instruction set + channels) a problem will be evolved with.
MachineSpec make_machine(const Problem& problem, const EngineConfig& cfg);

class Engine {
public:
    Engine(const Problem& problem, EngineConfig cfg);
    void on_generation(std::function<void(const GenerationReport&)> cb) { callback_ = std::move(cb); }
    EvolutionResult run();
    const MachineSpec& machine() const { return spec_; }

private:
    const Problem& problem_;
    EngineConfig cfg_;
    MachineSpec spec_;
    std::function<void(const GenerationReport&)> callback_;
};

/// Run fn(i) for i in [0, n) on up to `threads` threads (0 = hardware concurrency).
void parallel_for(std::size_t n, int threads, const std::function<void(std::size_t)>& fn);

const char* to_string(SelectionMethod m);

}  // namespace adaptogen
