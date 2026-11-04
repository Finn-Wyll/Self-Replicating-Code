#include "adaptogen/simplify.hpp"

#include <algorithm>
#include <functional>

namespace adaptogen {

namespace {

bool is_if(Op op) { return op == Op::IfLt || op == Op::IfGe || op == Op::IfEq; }

// A statement: an instruction plus, for if/loop, the statements it controls.
struct Node {
    Instr in;
    std::vector<Node> body;
};

std::size_t parse(const MachineSpec& spec, const std::vector<Instr>& code, std::size_t i, Node& out) {
    out.in = code[i];
    out.body.clear();
    const std::size_t n = code.size();
    if (is_if(out.in.op)) {
        if (i + 1 < n) {
            out.body.emplace_back();
            return parse(spec, code, i + 1, out.body.back());
        }
        return i + 1;
    }
    if (out.in.op == Op::Loop) {
        const int count = std::clamp<int>(out.in.c, 1, spec.limits.max_loop_body);
        std::size_t j = i + 1;
        for (int k = 0; k < count && j < n; ++k) {
            out.body.emplace_back();
            j = parse(spec, code, j, out.body.back());
        }
        return j;
    }
    return i + 1;
}

std::vector<Node> parse_program(const MachineSpec& spec, const Program& p) {
    std::vector<Node> nodes;
    for (std::size_t i = 0; i < p.code.size();) {
        nodes.emplace_back();
        i = parse(spec, p.code, i, nodes.back());
    }
    return nodes;
}

// Flatten back to instructions. Ifs/loops whose body became empty are dropped (an encoding of
// an empty body would silently capture the following statement).
void flatten(const std::vector<Node>& nodes, std::vector<Instr>& out) {
    for (const Node& nd : nodes) {
        if ((is_if(nd.in.op) || nd.in.op == Op::Loop) && nd.body.empty()) continue;
        Instr in = nd.in;
        if (in.op == Op::Loop) in.c = static_cast<std::uint8_t>(nd.body.size());
        out.push_back(in);
        flatten(nd.body, out);
    }
}

Program to_program(const std::vector<Node>& nodes, double mutation_rate) {
    Program p;
    p.mutation_rate = mutation_rate;
    flatten(nodes, p.code);
    return p;
}

// Visit every statement as (container, index).
void each_slot(std::vector<Node>& nodes, const std::function<void(std::vector<Node>&, std::size_t)>& fn) {
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        each_slot(nodes[i].body, fn);
        fn(nodes, i);
    }
}

}  // namespace

SimplifyResult simplify(const Problem& problem, const MachineSpec& spec, const Program& program,
                        const SimplifyOptions& opt) {
    SimplifyResult res;
    res.length_before = program.size();
    const std::size_t C = problem.num_cases(opt.split);

    std::vector<std::size_t> all_cases(C);
    for (std::size_t c = 0; c < C; ++c) all_cases[c] = c;
    auto scores = [&](const Program& p) {
        std::vector<double> s;
        std::vector<long> steps;
        problem.evaluate_batch(spec, {&p}, opt.split, all_cases, 1, s, steps);  // one round trip for plug-ins
        ++res.program_evaluations;
        return s;
    };
    const std::vector<double> reference = scores(program);
    auto acceptable = [&](const Program& p) {
        const auto s = scores(p);
        for (std::size_t c = 0; c < C; ++c)
            if (s[c] < reference[c] - opt.tolerance) return false;
        return true;
    };

    // The program is in canonical flattened form from here on (drops ifs/loops with empty bodies
    // only if that is harmless).
    std::vector<Node> tree = parse_program(spec, program);
    Program current = to_program(tree, program.mutation_rate);
    if (!acceptable(current)) {
        tree = parse_program(spec, program);  // keep the original encoding exactly
        current = program;
    }

    for (int pass = 0; pass < opt.max_passes; ++pass) {
        bool changed = false;
        // Collect candidate edits first (paths change as we edit), then try them one by one.
        std::size_t slots = 0;
        each_slot(tree, [&](std::vector<Node>&, std::size_t) { ++slots; });
        for (std::size_t target = slots; target-- > 0;) {
            for (int kind = 0; kind < 2; ++kind) {  // 0 = delete statement, 1 = unwrap if/loop
                std::vector<Node> trial = tree;
                std::size_t k = 0;
                bool applied = false;
                each_slot(trial, [&](std::vector<Node>& container, std::size_t i) {
                    if (applied || k++ != target) return;
                    if (kind == 0) {
                        container.erase(container.begin() + static_cast<long>(i));
                        applied = true;
                    } else if (!container[i].body.empty()) {
                        std::vector<Node> inner = std::move(container[i].body);
                        container.erase(container.begin() + static_cast<long>(i));
                        container.insert(container.begin() + static_cast<long>(i), inner.begin(), inner.end());
                        applied = true;
                    }
                });
                if (!applied) continue;
                const Program candidate = to_program(trial, program.mutation_rate);
                if (candidate.size() >= current.size()) continue;
                if (!acceptable(candidate)) continue;
                tree = std::move(trial);
                current = candidate;
                ++res.accepted_changes;
                changed = true;
                break;
            }
        }
        if (!changed) break;
    }
    res.program = current;
    res.length_after = current.size();
    return res;
}

}  // namespace adaptogen
