#include "adaptogen/analysis.hpp"

#include <cstdio>
#include <sstream>

namespace adaptogen {

namespace {

bool is_if(Op op) { return op == Op::IfLt || op == Op::IfGe || op == Op::IfEq; }

// Registers read by an instruction.
void sources(const MachineSpec& spec, const Instr& in, std::vector<int>& out) {
    out.clear();
    switch (in.op) {
        case Op::Sense:
        case Op::Act: {
            if (in.channel >= spec.channels.size()) return;
            const int arity = spec.channels[in.channel].arity;
            const int regs[3] = {in.a, in.b, in.c};
            for (int k = 0; k < arity && k < 3; ++k) out.push_back(regs[k]);
            return;
        }
        case Op::Loop: out.push_back(in.a); return;
        default: {
            const int s = op_info(in.op).sources;
            if (s >= 1) out.push_back(in.a);
            if (s >= 2) out.push_back(in.b);
        }
    }
}

// conditional[i] = instruction i only executes under an If or inside a Loop body.
std::size_t mark_conditional(const MachineSpec& spec, const Program& p, std::size_t i, bool cond,
                             std::vector<bool>& conditional) {
    const std::size_t n = p.code.size();
    if (i >= n) return n;
    conditional[i] = cond;
    const Instr& in = p.code[i];
    if (is_if(in.op)) return mark_conditional(spec, p, i + 1, true, conditional);
    if (in.op == Op::Loop) {
        const std::size_t end = statement_end(spec, p, i);
        for (std::size_t j = i + 1; j < end;) j = mark_conditional(spec, p, j, true, conditional);
        return end;
    }
    return i + 1;
}

}  // namespace

std::vector<bool> effective_mask(const MachineSpec& spec, const Program& p, int num_outputs) {
    const std::size_t n = p.code.size();
    std::vector<bool> conditional(n, false), mask(n, false);
    for (std::size_t i = 0; i < n;) i = mark_conditional(spec, p, i, false, conditional);

    std::vector<std::size_t> loop_end(n, 0);
    for (std::size_t i = 0; i < n; ++i)
        if (p.code[i].op == Op::Loop) loop_end[i] = statement_end(spec, p, i);

    const int R = spec.num_registers;
    std::vector<int> src;
    for (int pass = 0; pass < 12; ++pass) {
        std::vector<bool> needed(R, false), next(n, false);
        for (int k = 0; k < num_outputs && k < R; ++k) needed[k] = true;
        // Registers read inside effective loop bodies are needed again at the end of the body
        // (the next iteration reads them).
        std::vector<std::vector<int>> inject(n + 1);
        for (std::size_t i = 0; i < n; ++i) {
            if (p.code[i].op != Op::Loop || !mask[i] || loop_end[i] <= i + 1) continue;
            for (std::size_t j = i + 1; j < loop_end[i]; ++j) {
                if (!mask[j]) continue;
                sources(spec, p.code[j], src);
                inject[loop_end[i]].insert(inject[loop_end[i]].end(), src.begin(), src.end());
            }
        }
        for (std::size_t ii = n; ii-- > 0;) {
            for (int r : inject[ii + 1]) needed[r] = true;
            const Instr& in = p.code[ii];
            bool eff = false;
            if (is_if(in.op)) {
                const std::size_t end = statement_end(spec, p, ii + 1 < n ? ii + 1 : n);
                for (std::size_t j = ii + 1; j < end; ++j) eff = eff || next[j];
            } else if (in.op == Op::Loop) {
                for (std::size_t j = ii + 1; j < loop_end[ii]; ++j) eff = eff || next[j];
            } else if (in.op == Op::Act) {
                eff = in.channel < spec.channels.size();
            } else if (in.op != Op::Nop && op_info(in.op).writes_dst) {
                eff = needed[in.dst];
            }
            if (!eff) continue;
            next[ii] = true;
            if (!is_if(in.op) && in.op != Op::Loop && !conditional[ii]) needed[in.dst] = false;
            sources(spec, in, src);
            for (int r : src) needed[r] = true;
        }
        if (next == mask) break;
        for (std::size_t i = 0; i < n; ++i) mask[i] = mask[i] || next[i];
    }
    return mask;
}

std::size_t effective_length(const MachineSpec& spec, const Program& p, int num_outputs) {
    std::size_t k = 0;
    for (bool b : effective_mask(spec, p, num_outputs)) k += b;
    return k;
}

std::string format_instruction(const MachineSpec& spec, const Instr& in) {
    auto reg = [](int r) { return "r" + std::to_string(r); };
    auto num = [](double v) {
        char buf[32];
        std::snprintf(buf, sizeof buf, "%.6g", v);
        return std::string(buf);
    };
    const std::string d = reg(in.dst), a = reg(in.a), b = reg(in.b);
    switch (in.op) {
        case Op::Nop: return "nop";
        case Op::Const: return d + " = " + num(in.imm);
        case Op::Mov: return d + " = " + a;
        case Op::Add: return d + " = " + a + " + " + b;
        case Op::Sub: return d + " = " + a + " - " + b;
        case Op::Mul: return d + " = " + a + " * " + b;
        case Op::Div: return d + " = " + a + " / " + b;
        case Op::Min: return d + " = min(" + a + ", " + b + ")";
        case Op::Max: return d + " = max(" + a + ", " + b + ")";
        case Op::Neg: return d + " = -" + a;
        case Op::Abs: return d + " = abs(" + a + ")";
        case Op::Sin: return d + " = sin(" + a + ")";
        case Op::Cos: return d + " = cos(" + a + ")";
        case Op::Exp: return d + " = exp(" + a + ")";
        case Op::Log: return d + " = log(|" + a + "|)";
        case Op::Sqrt: return d + " = sqrt(|" + a + "|)";
        case Op::IfLt: return "if " + a + " < " + b + ":";
        case Op::IfGe: return "if " + a + " >= " + b + ":";
        case Op::IfEq: return "if " + a + " == " + b + ":";
        case Op::Loop: return "repeat |" + a + "| times (" + d + " = 0, 1, ...):";
        case Op::Sense:
        case Op::Act: {
            if (in.channel >= spec.channels.size()) return "nop  # unknown channel";
            const Channel& ch = spec.channels[in.channel];
            const int regs[3] = {in.a, in.b, in.c};
            std::string args;
            for (int k = 0; k < ch.arity && k < 3; ++k) args += (k ? ", " : "") + reg(regs[k]);
            if (in.op == Op::Sense) return d + " = " + ch.name + (ch.arity ? "[" + args + "]" : "");
            return d + " = " + ch.name + "(" + args + ")";
        }
        case Op::Count_: break;
    }
    return "?";
}

namespace {
std::size_t print_stmt(const MachineSpec& spec, const Program& p, const std::vector<bool>& mask,
                       bool effective_only, std::size_t i, int indent, std::ostringstream& out) {
    const std::size_t n = p.code.size();
    if (i >= n) return n;
    const std::size_t end = statement_end(spec, p, i);
    if (effective_only && !mask[i]) return end;
    const Instr& in = p.code[i];
    out << std::string(indent * 4, ' ') << format_instruction(spec, in);
    if (!effective_only && !mask[i]) out << "    # no effect";
    out << '\n';
    if (is_if(in.op) || in.op == Op::Loop) {
        if (i + 1 >= end) out << std::string((indent + 1) * 4, ' ') << "pass\n";
        for (std::size_t j = i + 1; j < end;) j = print_stmt(spec, p, mask, effective_only, j, indent + 1, out);
    }
    return end;
}
}  // namespace

std::string to_pseudocode(const MachineSpec& spec, const Program& p, int num_outputs, bool effective_only) {
    const auto mask = effective_mask(spec, p, num_outputs);
    std::ostringstream out;
    out << "# all registers start at 0";
    if (num_outputs > 0) {
        out << "; output: ";
        for (int k = 0; k < num_outputs; ++k) out << (k ? ", " : "") << "r" << k;
    }
    out << '\n';
    for (std::size_t i = 0; i < p.code.size();) i = print_stmt(spec, p, mask, effective_only, i, 0, out);
    return out.str();
}

}  // namespace adaptogen
