#include "adaptogen/instruction.hpp"

#include <array>

namespace adaptogen {

namespace {
constexpr std::array<OpInfo, kNumOps> kOps = {{
    {"nop", 0, false},  {"const", 0, true}, {"mov", 1, true},   {"add", 2, true},
    {"sub", 2, true},   {"mul", 2, true},   {"div", 2, true},   {"min", 2, true},
    {"max", 2, true},   {"neg", 1, true},   {"abs", 1, true},   {"sin", 1, true},
    {"cos", 1, true},   {"exp", 1, true},   {"log", 1, true},   {"sqrt", 1, true},
    {"iflt", 2, false}, {"ifge", 2, false}, {"ifeq", 2, false}, {"loop", 1, true},
    {"sense", -1, true}, {"act", -1, true},
}};
}  // namespace

const OpInfo& op_info(Op op) { return kOps[static_cast<std::size_t>(op)]; }

std::optional<Op> op_from_name(std::string_view name) {
    for (int i = 0; i < kNumOps; ++i)
        if (kOps[i].name == name) return static_cast<Op>(i);
    return std::nullopt;
}

std::vector<Op> core_ops() {
    std::vector<Op> ops;
    for (int i = static_cast<int>(Op::Const); i <= static_cast<int>(Op::Loop); ++i) ops.push_back(static_cast<Op>(i));
    return ops;
}

std::vector<Op> classic_regression_ops() {
    return {Op::Const, Op::Add, Op::Sub, Op::Mul, Op::Div, Op::Sin, Op::Cos, Op::Exp, Op::Log};
}

void canonicalize(Instr& in) {
    const OpInfo& info = op_info(in.op);
    switch (in.op) {
        case Op::Nop: in = Instr{}; return;
        case Op::Sense:
        case Op::Act: in.imm = 0; return;
        case Op::Loop: in.b = 0; in.imm = 0; in.channel = 0; return;
        case Op::IfLt: case Op::IfGe: case Op::IfEq: in.dst = 0; in.c = 0; in.imm = 0; in.channel = 0; return;
        default:
            if (info.sources < 2) in.b = 0;
            if (info.sources < 1) in.a = 0;
            if (in.op != Op::Const) in.imm = 0;
            in.c = 0;
            in.channel = 0;
    }
}

}  // namespace adaptogen
