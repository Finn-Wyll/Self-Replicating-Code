#pragma once
#include <functional>
#include <string>
#include <vector>

#include "adaptogen/problem.hpp"
#include "adaptogen/variation.hpp"

namespace adaptogen {

enum class SelectionMethod { Tournament, Lexicase };

/// The bacteria-inspired mechanisms. Each is off by default so its effect can be measured.
struct Mechanisms {
    /// SOS response: when progress stalls for `patience` generations, offspring get extra
    /// mutations: multiplier = 1 + growth * (stalled generations - patience), capped.
    /// trigger "lineage": each lineage's own stagnation (no improvement over its parent);
    /// trigger "population": the whole population, when the best program stops improving.
    struct Stress {
        bool enabled = false;
        std::string trigger = "lineage";
        int patience = 3;
        double growth = 0.5;
        double max_multiplier = 6.0;
    } stress;
    /// Hypermutators: every program carries a heritable mutation rate (expected mutations per
    /// offspring) which itself mutates, so evolution tunes its own exploration.
    struct Hypermutation {
        bool enabled = false;
        double initial_rate = 1.0;
        double min_rate = 0.2, max_rate = 8.0;
        double p_change = 0.1;     // chance per offspring that the rate itself mutates
        double change_sigma = 0.5; // log-normal step size
    } hypermutation;
    /// Horizontal gene transfer: copy a short segment from a random (neighbouring) donor.
    struct Transfer {
        bool enabled = false;
        double rate = 0.2;  // chance per offspring
        int max_segment = 6;
    } transfer;
    /// Gene duplication: copy a segment in place; the copies can then diverge.
    struct Duplication {
        bool enabled = false;
        double rate = 0.1;
        int max_segment = 6;
    } duplication;
    /// Spatial structure: population on a torus; offspring compete locally (replace the cell
    /// only if at least as good), which preserves diversity like colonies on a plate.
    struct Spatial {
        bool enabled = false;
        int width = 0;   // 0 = square grid sized to the population
        int radius = 1;  // neighbourhood radius (1 = 3x3)
    } spatial;
    /// MEGA-plate curriculum (requires spatial): the grid's columns form bands; band k is
    /// judged on the easiest (k+1)/bands fraction of the training cases, the last on all.
    /// Case difficulty is measured from how well the population does on each case.
    struct Curriculum {
        bool enabled = false;
        int bands = 4;
        int refresh = 5;  // re-rank case difficulty every N generations
    } curriculum;
};

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
    Mechanisms mechanisms;
    double const_min = -5.0, const_max = 5.0;
    double length_cost = 0.0;  // fitness penalty per instruction ("cost of resistance")
    double time_cost = 0.0;    // fitness penalty per executed instruction per case
    int threads = 0;           // 0 = hardware concurrency
    bool stop_when_solved = true;
    long max_program_evaluations = 0;  // 0 = no budget beyond generations
    /// Fraction of the training cases held back to choose the final program (guards against
    /// memorisation). 0 = off: the best program on all training cases wins.
    double validation_fraction = 0.0;

    /// Start from saved strains instead of (only) random programs. Resolved by run_evolution()
    /// in strain.hpp, which loads the strains from the library; the Engine itself takes programs.
    struct Seeding {
        std::vector<std::string> strains;  // strain ids ("name-v2") or names (latest version)
        std::string library = "strains";   // strain library folder
        double random_fraction = 0.1;      // share of the population that starts random (diversity)
        double retain_weight = 0.0;        // keep scoring on the parent strains' problems (0 = specialise)
        std::vector<std::pair<std::string, std::string>> channel_map;  // strain channel -> problem channel
    } seeding;
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
    /// Population-level statistics of the mechanisms (mutation rates, stress, transfers...).
    std::vector<Metric> population_stats;
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

    /// Seed the initial population: (1 - random_fraction) of it are copies of these programs
    /// (round-robin; one exact copy of each, the others mutated once), the rest random.
    void set_seed_programs(std::vector<Program> programs, double random_fraction) {
        seeds_ = std::move(programs);
        seed_random_fraction_ = random_fraction;
    }

private:
    const Problem& problem_;
    EngineConfig cfg_;
    MachineSpec spec_;
    std::vector<Program> seeds_;
    double seed_random_fraction_ = 0.1;
    std::function<void(const GenerationReport&)> callback_;
};

/// Run fn(i) for i in [0, n) on up to `threads` threads (0 = hardware concurrency).
void parallel_for(std::size_t n, int threads, const std::function<void(std::size_t)>& fn);

const char* to_string(SelectionMethod m);

}  // namespace adaptogen
