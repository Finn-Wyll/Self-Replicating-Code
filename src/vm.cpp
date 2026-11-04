#include "adaptogen/vm.hpp"

#include <algorithm>
#include <cmath>

namespace adaptogen {

void MachineSpec::finalize() {
    sensors.clear();
    actions.clear();
    for (int i = 0; i < static_cast<int>(channels.size()); ++i)
        (channels[i].kind == ChannelKind::Sensor ? sensors : actions).push_back(i);
    if (num_registers < 1) num_registers = 1;
    if (num_registers > 255) num_registers = 255;
}

namespace {

inline double clean(double v) {
    if (std::isnan(v)) return 0.0;
    return std::clamp(v, -1e15, 1e15);
}

// Protected operators: always defined, so every program runs.
inline double pdiv(double a, double b) { return std::fabs(b) < 1e-9 ? 1.0 : a / b; }
inline double plog(double a) { return std::fabs(a) < 1e-9 ? 0.0 : std::log(std::fabs(a)); }
inline double pexp(double a) { return std::exp(std::min(a, 50.0)); }

struct Exec {
    const MachineSpec& spec;
    const std::vector<Instr>& code;
    Environment& env;
    double* r;
    int steps = 0;
    bool hit = false;

    std::size_t end_of(std::size_t i) const {
        const std::size_t n = code.size();
        if (i >= n) return n;
        const Op op = code[i].op;
        if (op == Op::IfLt || op == Op::IfGe || op == Op::IfEq) return end_of(i + 1);
        if (op == Op::Loop) {
            std::size_t j = i + 1;
            const int body = std::clamp<int>(code[i].c, 1, spec.limits.max_loop_body);
            for (int k = 0; k < body && j < n; ++k) j = end_of(j);
            return j;
        }
        return i + 1;
    }

    // Executes the statement at i and returns the index after it.
    std::size_t exec(std::size_t i) {
        const std::size_t n = code.size();
        if (steps >= spec.limits.max_steps) {
            hit = true;
            return n;
        }
        ++steps;
        const Instr& in = code[i];
        double* d = &r[in.dst];
        const double a = r[in.a], b = r[in.b];
        switch (in.op) {
            case Op::Nop: break;
            case Op::Const: *d = in.imm; break;
            case Op::Mov: *d = a; break;
            case Op::Add: *d = clean(a + b); break;
            case Op::Sub: *d = clean(a - b); break;
            case Op::Mul: *d = clean(a * b); break;
            case Op::Div: *d = clean(pdiv(a, b)); break;
            case Op::Min: *d = std::min(a, b); break;
            case Op::Max: *d = std::max(a, b); break;
            case Op::Neg: *d = -a; break;
            case Op::Abs: *d = std::fabs(a); break;
            case Op::Sin: *d = std::sin(a); break;
            case Op::Cos: *d = std::cos(a); break;
            case Op::Exp: *d = clean(pexp(a)); break;
            case Op::Log: *d = plog(a); break;
            case Op::Sqrt: *d = std::sqrt(std::fabs(a)); break;
            case Op::IfLt:
            case Op::IfGe:
            case Op::IfEq: {
                if (i + 1 >= n) return n;
                bool cond = in.op == Op::IfLt ? a < b : in.op == Op::IfGe ? a >= b : std::fabs(a - b) < 1e-9;
                return cond ? exec(i + 1) : end_of(i + 1);
            }
            case Op::Loop: {
                const std::size_t body_end = end_of(i);
                const int count = static_cast<int>(std::min<double>(std::fabs(a), spec.limits.max_loop_iterations));
                for (int k = 0; k < count && !hit; ++k) {
                    r[in.dst] = k;
                    for (std::size_t j = i + 1; j < body_end && !hit;) j = exec(j);
                }
                return hit ? n : body_end;
            }
            case Op::Sense:
            case Op::Act: {
                if (in.channel >= spec.channels.size()) break;
                const Channel& ch = spec.channels[in.channel];
                const bool want_sensor = in.op == Op::Sense;
                if ((ch.kind == ChannelKind::Sensor) != want_sensor) break;
                const double args[3] = {a, b, r[in.c]};
                const int nargs = std::clamp(ch.arity, 0, 3);
                *d = clean(want_sensor ? env.sense(in.channel, args, nargs) : env.act(in.channel, args, nargs));
                break;
            }
            case Op::Count_: break;
        }
        return i + 1;
    }
};

}  // namespace

RunStats run(const MachineSpec& spec, const Program& program, Environment& env, std::span<double> regs) {
    Exec ex{spec, program.code, env, regs.data()};
    const std::size_t n = program.code.size();
    for (std::size_t i = 0; i < n && !ex.hit;) i = ex.exec(i);
    return {ex.steps, ex.hit};
}

std::size_t statement_end(const MachineSpec& spec, const Program& program, std::size_t i) {
    struct NullEnv : Environment {
        double sense(int, const double*, int) override { return 0; }
    } env;
    Exec ex{spec, program.code, env, nullptr};
    return ex.end_of(i);
}

}  // namespace adaptogen
