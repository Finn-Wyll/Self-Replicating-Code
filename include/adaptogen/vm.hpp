#pragma once
#include <span>
#include <vector>

#include "adaptogen/channel.hpp"
#include "adaptogen/instruction.hpp"

namespace adaptogen {

struct VmLimits {
    int max_steps = 2000;            // hard cap on executed instructions per run (no infinite loops)
    int max_loop_iterations = 256;   // cap on iterations of a single loop
    int max_loop_body = 4;           // a loop body is 1..max_loop_body statements
};

/// Implemented by a problem: answers sensor reads and performs actions.
class Environment {
public:
    virtual ~Environment() = default;
    virtual double sense(int channel, const double* args, int nargs) = 0;
    virtual double act(int channel, const double* args, int nargs) {
        (void)channel; (void)args; (void)nargs;
        return 0.0;
    }
};

/// Everything needed to generate and run programs for one problem.
struct MachineSpec {
    int num_registers = 8;
    std::vector<Op> ops;            // general-purpose ops evolution may use
    std::vector<Channel> channels;  // from the problem
    VmLimits limits;
    double const_min = -5.0, const_max = 5.0;

    // derived (see finalize)
    std::vector<int> sensors, actions;
    void finalize();
};

struct RunStats {
    int steps = 0;
    bool step_limit_hit = false;
};

/// Execute a program. regs must hold at least spec.num_registers values; they are
/// used as-is (the caller decides how they are initialised and which are outputs).
RunStats run(const MachineSpec& spec, const Program& program, Environment& env, std::span<double> regs);

/// Index just past the statement starting at i (a statement is an instruction, an
/// If plus its guarded statement, or a Loop plus its body).
std::size_t statement_end(const MachineSpec& spec, const Program& program, std::size_t i);

}  // namespace adaptogen
