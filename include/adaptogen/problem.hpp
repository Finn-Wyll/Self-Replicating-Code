#pragma once
// The single generic problem interface. There are no domain base classes in the engine:
// a problem only declares channels (what can be sensed / done) and scores outcomes.
#include <span>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "adaptogen/channel.hpp"
#include "adaptogen/instruction.hpp"
#include "adaptogen/vm.hpp"

namespace adaptogen {

enum class Split { Train, Test };

struct Metric {
    std::string name;
    double value = 0.0;
};

/// Runs programs on behalf of a problem and counts the work done (used for time costs).
class Runner {
public:
    explicit Runner(const MachineSpec& spec) : spec_(spec) {}
    RunStats run(const Program& p, Environment& env, std::span<double> regs) {
        RunStats s = adaptogen::run(spec_, p, env, regs);
        steps_ += s.steps;
        ++runs_;
        return s;
    }
    const MachineSpec& spec() const { return spec_; }
    void add_steps(long s) { steps_ += s; }
    long steps() const { return steps_; }
    long runs() const { return runs_; }

private:
    const MachineSpec& spec_;
    long steps_ = 0;
    long runs_ = 0;
};

class Problem {
public:
    virtual ~Problem() = default;

    virtual std::string name() const = 0;
    virtual std::string description() const = 0;
    virtual std::vector<Channel> channels() const = 0;

    /// Registers r0..r(n-1) that the problem reads as output after a run (if any).
    virtual int num_outputs() const { return 1; }
    /// Instruction set used when the user does not choose one.
    virtual std::vector<Op> default_ops() const { return core_ops(); }

    /// Cases are independent evaluations (data points, random instances, episodes...).
    /// Train cases drive evolution; test cases are unseen and measure generalisation.
    virtual std::size_t num_cases(Split split) const = 0;

    /// Score one case; higher is better. Must be thread-safe (called concurrently).
    virtual double evaluate_case(Runner& runner, const Program& program, Split split,
                                 std::size_t index) const = 0;

    /// Human-readable metrics. Default: mean case score.
    virtual std::vector<Metric> metrics(const MachineSpec& spec, const Program& program, Split split) const;

    /// Whether a program counts as a full solution (used for early stopping and success rates).
    virtual bool solved(const MachineSpec& spec, const Program& program, Split split) const {
        (void)spec; (void)program; (void)split;
        return false;
    }

    /// Extra metadata saved with a strain evolved on this problem (e.g. confidence calibration).
    virtual nlohmann::json strain_extras(const MachineSpec& spec, const Program& program) const {
        (void)spec; (void)program;
        return nlohmann::json::object();
    }

    /// Mean case score over a split.
    double mean_score(const MachineSpec& spec, const Program& program, Split split) const;

    /// Score several programs on the same cases: scores is programs x cases (row-major), steps the
    /// executed instructions per program. The engine scores everything through this, so a problem
    /// living elsewhere (a plug-in in a worker process) can ship whole batches at once.
    /// Default: evaluate_case for every (program, case), programs in parallel.
    virtual void evaluate_batch(const MachineSpec& spec, const std::vector<const Program*>& programs, Split split,
                                const std::vector<std::size_t>& cases, int threads, std::vector<double>& scores,
                                std::vector<long>& steps) const;
};

}  // namespace adaptogen
