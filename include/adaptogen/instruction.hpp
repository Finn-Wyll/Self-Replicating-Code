#pragma once
// The instruction set of the virtual machine. Any sequence of these is a valid
// program, so mutation can never produce code that fails to run.
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace adaptogen {

enum class Op : std::uint8_t {
    Nop,    // no effect (also used for pseudogenes: references to unavailable channels)
    Const,  // r[dst] = imm
    Mov,    // r[dst] = r[a]
    Add, Sub, Mul, Div, Min, Max,  // r[dst] = r[a] op r[b]   (division is protected)
    Neg, Abs, Sin, Cos, Exp, Log, Sqrt,  // r[dst] = f(r[a])   (protected)
    IfLt, IfGe, IfEq,  // execute the next statement only if r[a] <op> r[b]
    Loop,   // repeat the next c statements |r[a]| times, writing the iteration index to r[dst]
    Sense,  // r[dst] = sensor[channel](r[a], r[b], r[c])
    Act,    // r[dst] = action[channel](r[a], r[b], r[c])   (returns feedback)
    Count_
};

inline constexpr int kNumOps = static_cast<int>(Op::Count_);

struct OpInfo {
    std::string_view name;
    int sources;      // number of register sources (a, b); -1 = depends on channel arity
    bool writes_dst;
};

const OpInfo& op_info(Op op);
std::optional<Op> op_from_name(std::string_view name);

/// All general-purpose ops (everything except Nop/Sense/Act).
std::vector<Op> core_ops();
/// The classic symbolic-regression function set {+,-,*,/,sin,cos,exp,log} plus constants.
std::vector<Op> classic_regression_ops();

struct Instr {
    Op op = Op::Nop;
    std::uint8_t dst = 0, a = 0, b = 0, c = 0;  // for Loop, c = body length in statements
    std::uint16_t channel = 0;
    double imm = 0.0;
    bool operator==(const Instr&) const = default;
};

/// Zero the fields an op does not use, so equal behaviour means equal encoding.
void canonicalize(Instr& in);

struct Program {
    std::vector<Instr> code;
    /// Heritable mutation rate (expected mutations per offspring) used when hypermutation is on.
    double mutation_rate = 1.0;
    bool operator==(const Program&) const = default;
    std::size_t size() const { return code.size(); }
};

}  // namespace adaptogen
