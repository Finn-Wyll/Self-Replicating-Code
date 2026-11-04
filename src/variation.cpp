#include "adaptogen/variation.hpp"

#include <algorithm>

#include "adaptogen/analysis.hpp"

namespace adaptogen {

namespace {
std::uint8_t reg(const MachineSpec& spec, Rng& rng) {
    return static_cast<std::uint8_t>(rng.below(static_cast<std::uint64_t>(spec.num_registers)));
}
}  // namespace

Instr random_instruction(const MachineSpec& spec, const VariationConfig& cfg, Rng& rng) {
    // Pick an op: each general op has weight 1, Sense/Act have configurable weights.
    const double ws = spec.sensors.empty() ? 0.0 : cfg.sense_weight;
    const double wa = spec.actions.empty() ? 0.0 : cfg.act_weight;
    const double total = static_cast<double>(spec.ops.size()) + ws + wa;
    double u = rng.uniform() * total;
    Instr in;
    if (u < ws) {
        in.op = Op::Sense;
        in.channel = static_cast<std::uint16_t>(spec.sensors[rng.below(spec.sensors.size())]);
    } else if (u < ws + wa) {
        in.op = Op::Act;
        in.channel = static_cast<std::uint16_t>(spec.actions[rng.below(spec.actions.size())]);
    } else if (!spec.ops.empty()) {
        in.op = spec.ops[rng.below(spec.ops.size())];
    }
    in.dst = reg(spec, rng);
    in.a = reg(spec, rng);
    in.b = reg(spec, rng);
    in.c = in.op == Op::Loop ? static_cast<std::uint8_t>(rng.range(1, std::max(1, spec.limits.max_loop_body)))
                             : reg(spec, rng);
    if (in.op == Op::Const) in.imm = rng.uniform(spec.const_min, spec.const_max);
    canonicalize(in);
    return in;
}

Program random_program(const MachineSpec& spec, const VariationConfig& cfg, Rng& rng) {
    Program p;
    const int len = rng.range(std::max(1, cfg.init_min_length), std::max(1, cfg.init_max_length));
    p.code.reserve(len);
    for (int i = 0; i < len; ++i) p.code.push_back(random_instruction(spec, cfg, rng));
    return p;
}

namespace {

// Registers that are read by effective code at or after position pos (or are outputs):
// writing one of them at pos makes the new instruction effective.
std::vector<int> useful_registers(const MachineSpec& spec, const Program& p, const std::vector<bool>& mask,
                                  std::size_t pos, int num_outputs) {
    std::vector<bool> used(static_cast<std::size_t>(spec.num_registers), false);
    for (int k = 0; k < num_outputs && k < spec.num_registers; ++k) used[static_cast<std::size_t>(k)] = true;
    for (std::size_t j = pos; j < p.code.size(); ++j) {
        if (!mask[j]) continue;
        const Instr& in = p.code[j];
        const int s = in.op == Op::Sense || in.op == Op::Act ? 3 : in.op == Op::Loop ? 1 : op_info(in.op).sources;
        if (s >= 1) used[in.a] = true;
        if (s >= 2) used[in.b] = true;
        if (s >= 3) used[in.c] = true;
    }
    std::vector<int> out;
    for (int r = 0; r < spec.num_registers; ++r)
        if (used[static_cast<std::size_t>(r)]) out.push_back(r);
    return out;
}

std::size_t pick(const std::vector<bool>& mask, bool effective, Rng& rng) {
    std::vector<std::size_t> idx;
    for (std::size_t i = 0; i < mask.size(); ++i)
        if (!effective || mask[i]) idx.push_back(i);
    if (idx.empty()) return rng.below(mask.size());
    return idx[rng.below(idx.size())];
}

}  // namespace

MutationKind mutate(Program& p, const MachineSpec& spec, const VariationConfig& cfg, Rng& rng, int num_outputs) {
    const double total = cfg.w_insert + cfg.w_delete + cfg.w_point + cfg.w_constant;
    if (total <= 0) return MutationKind::None;
    double u = rng.uniform() * total;
    auto& code = p.code;
    const int max_len = std::max(1, cfg.max_length);
    const bool eff = cfg.effective_mutation;
    std::vector<bool> mask = eff ? effective_mask(spec, p, num_outputs) : std::vector<bool>(code.size(), true);

    if ((u -= cfg.w_insert) < 0) {
        if (static_cast<int>(code.size()) >= max_len) return MutationKind::None;
        const std::size_t pos = rng.below(code.size() + 1);
        Instr in = random_instruction(spec, cfg, rng);
        if (eff && op_info(in.op).writes_dst && in.op != Op::Act) {
            const auto useful = useful_registers(spec, p, mask, pos, num_outputs);
            if (!useful.empty()) in.dst = static_cast<std::uint8_t>(useful[rng.below(useful.size())]);
            canonicalize(in);
        }
        code.insert(code.begin() + static_cast<long>(pos), in);
        return MutationKind::Insertion;
    }
    if ((u -= cfg.w_delete) < 0) {
        if (code.size() <= 1) return MutationKind::None;
        code.erase(code.begin() + static_cast<long>(pick(mask, eff, rng)));
        return MutationKind::Deletion;
    }
    if (code.empty()) {
        code.push_back(random_instruction(spec, cfg, rng));
        return MutationKind::Insertion;
    }
    if ((u -= cfg.w_point) < 0) {
        Instr& in = code[pick(mask, eff, rng)];
        const std::uint8_t old_dst = in.dst;
        switch (rng.below(4)) {
            case 0:  // replace the whole instruction, keeping its destination so it stays effective
                in = random_instruction(spec, cfg, rng);
                if (eff && op_info(in.op).writes_dst) in.dst = old_dst;
                break;
            case 1: in.dst = reg(spec, rng); break;
            case 2: in.a = reg(spec, rng); break;
            default:
                if (in.op == Op::Const) in.imm = rng.uniform(spec.const_min, spec.const_max);
                else in.b = reg(spec, rng);
        }
        canonicalize(in);
        return MutationKind::Point;
    }
    // constant mutation: nudge one (effective) constant; falls back to a point mutation if none exist
    std::vector<std::size_t> consts;
    for (std::size_t i = 0; i < code.size(); ++i)
        if (code[i].op == Op::Const && (!eff || mask[i])) consts.push_back(i);
    if (consts.empty()) {
        code[pick(mask, eff, rng)] = random_instruction(spec, cfg, rng);
        return MutationKind::Point;
    }
    code[consts[rng.below(consts.size())]].imm += rng.normal() * cfg.constant_sigma;
    return MutationKind::Constant;
}

const char* to_string(MutationKind k) {
    switch (k) {
        case MutationKind::None: return "none";
        case MutationKind::Insertion: return "insertion";
        case MutationKind::Deletion: return "deletion";
        case MutationKind::Point: return "point mutation";
        case MutationKind::Constant: return "constant change";
    }
    return "?";
}

Program crossover(const Program& a, const Program& b, const VariationConfig& cfg, Rng& rng) {
    if (a.code.empty() || b.code.empty()) return a;
    const int la = static_cast<int>(a.code.size()), lb = static_cast<int>(b.code.size());
    const int sa = rng.range(1, la), sb = rng.range(1, lb);
    const int ia = rng.range(0, la - sa), ib = rng.range(0, lb - sb);
    Program child;
    child.mutation_rate = a.mutation_rate;
    child.code.reserve(static_cast<std::size_t>(la - sa + sb));
    child.code.insert(child.code.end(), a.code.begin(), a.code.begin() + ia);
    child.code.insert(child.code.end(), b.code.begin() + ib, b.code.begin() + ib + sb);
    child.code.insert(child.code.end(), a.code.begin() + ia + sa, a.code.end());
    if (static_cast<int>(child.code.size()) > cfg.max_length) child.code.resize(static_cast<std::size_t>(cfg.max_length));
    return child;
}

bool duplicate_segment(Program& p, const VariationConfig& cfg, int max_segment, Rng& rng) {
    const int n = static_cast<int>(p.code.size());
    if (n == 0) return false;
    const int len = rng.range(1, std::min(std::max(1, max_segment), n));
    if (n + len > cfg.max_length) return false;
    const int start = rng.range(0, n - len);
    std::vector<Instr> seg(p.code.begin() + start, p.code.begin() + start + len);
    p.code.insert(p.code.begin() + start + len, seg.begin(), seg.end());
    return true;
}

bool transfer_segment(Program& recipient, const Program& donor, const VariationConfig& cfg, int max_segment, Rng& rng) {
    const int n = static_cast<int>(donor.code.size());
    if (n == 0) return false;
    const int len = rng.range(1, std::min(std::max(1, max_segment), n));
    if (static_cast<int>(recipient.code.size()) + len > cfg.max_length) return false;
    const int start = rng.range(0, n - len);
    const long pos = static_cast<long>(rng.below(recipient.code.size() + 1));
    recipient.code.insert(recipient.code.begin() + pos, donor.code.begin() + start, donor.code.begin() + start + len);
    return true;
}

}  // namespace adaptogen
