#include "adaptogen/report.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <sstream>

#include "adaptogen/analysis.hpp"
#include "adaptogen/serialize.hpp"

namespace adaptogen {

namespace {

std::vector<std::string> lines_of(const std::string& text) {
    std::vector<std::string> out;
    std::istringstream in(text);
    for (std::string l; std::getline(in, l);)
        if (!l.empty() && l[0] != '#') out.push_back(l);  // skip the "# registers start at 0" header
    return out;
}

std::string num(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.6g", v);
    return buf;
}

}  // namespace

std::string program_diff(const MachineSpec& before_spec, const Program& before, const MachineSpec& after_spec,
                         const Program& after, int num_outputs) {
    const auto a = lines_of(to_pseudocode(before_spec, before, num_outputs, true));
    const auto b = lines_of(to_pseudocode(after_spec, after, num_outputs, true));
    // Longest common subsequence (programs are short).
    std::vector<std::vector<int>> L(a.size() + 1, std::vector<int>(b.size() + 1, 0));
    for (std::size_t i = a.size(); i-- > 0;)
        for (std::size_t j = b.size(); j-- > 0;)
            L[i][j] = a[i] == b[j] ? L[i + 1][j + 1] + 1 : std::max(L[i + 1][j], L[i][j + 1]);
    std::ostringstream out;
    std::size_t i = 0, j = 0;
    while (i < a.size() || j < b.size()) {
        if (i < a.size() && j < b.size() && a[i] == b[j]) {
            out << "  " << a[i++] << '\n';
            ++j;
        } else if (j < b.size() && (i == a.size() || L[i][j + 1] >= L[i + 1][j])) {
            out << "+ " << b[j++] << '\n';
        } else {
            out << "- " << a[i++] << '\n';
        }
    }
    return out.str();
}

std::string lineage_report(const MachineSpec& spec, int num_outputs, const EvolutionResult& result, const std::string& title) {
    std::ostringstream md;
    md << "# How the solution evolved: " << title << "\n\n"
       << result.lineage.size() << " improvements of the champion over " << result.generations << " generations ("
       << result.program_evaluations << " program evaluations). Each entry shows what produced the new champion "
       << "and how its effective code differs from its mother's (`+` added, `-` removed).\n\n"
       << "| # | generation | fitness | produced by |\n|---|---|---|---|\n";
    for (std::size_t k = 0; k < result.lineage.size(); ++k) {
        const auto& e = result.lineage[k];
        md << "| " << k + 1 << " | " << e.generation << " | " << num(e.fitness) << " | " << e.origin << " |\n";
    }
    for (std::size_t k = 0; k < result.lineage.size(); ++k) {
        const auto& e = result.lineage[k];
        md << "\n## " << k + 1 << ". generation " << e.generation << ": " << e.origin << " (fitness " << num(e.fitness) << ")\n\n```diff\n";
        if (e.has_mother) md << program_diff(spec, e.mother, spec, e.champion, num_outputs);
        else md << to_pseudocode(spec, e.champion, num_outputs, true);
        md << "```\n";
    }
    md << "\n## Final program\n\n```\n" << to_pseudocode(spec, result.best, num_outputs, true) << "```\n";
    return md.str();
}

MachineSpec strain_machine(const Strain& s) {
    MachineSpec spec;
    spec.num_registers = s.num_registers;
    spec.ops = s.ops;
    spec.channels = s.channels;
    if (s.config.contains("max_steps")) spec.limits.max_steps = s.config.at("max_steps").get<int>();
    if (s.config.contains("max_loop_iterations")) spec.limits.max_loop_iterations = s.config.at("max_loop_iterations").get<int>();
    if (s.config.contains("max_loop_body")) spec.limits.max_loop_body = s.config.at("max_loop_body").get<int>();
    spec.finalize();
    return spec;
}

std::string strain_lineage_report(const StrainLibrary& library, const std::string& id_or_name) {
    auto chain = library.ancestry(id_or_name);  // the strain first, then its ancestors
    std::reverse(chain.begin(), chain.end());   // oldest first
    std::ostringstream md;
    md << "# Lineage of " << chain.back().id << "\n\n"
       << "Oldest ancestor first. For each strain: the problem it evolved on, how well it did on unseen test "
       << "cases, and how its behaviour differs from its parent's.\n\n"
       << "| strain | problem | parents | test | size |\n|---|---|---|---|---|\n";
    for (const auto& s : chain) {
        std::string parents;
        for (const auto& p : s.parents) parents += (parents.empty() ? "" : ", ") + p;
        std::string test;
        for (const auto& [k, v] : s.test_metrics.items()) {
            if (!test.empty()) break;  // headline metric only
            test = k + " = " + num(v.get<double>());
        }
        const MachineSpec spec = strain_machine(s);
        const Program p = program_from_json(spec, s.program);
        md << "| " << s.id << " | " << s.problem.value("name", "?") << " | " << (parents.empty() ? "-" : parents) << " | "
           << test << (s.solved_test ? " (solved)" : "") << " | " << effective_length(spec, p, s.num_outputs) << " |\n";
    }
    for (std::size_t k = 0; k < chain.size(); ++k) {
        const Strain& s = chain[k];
        const MachineSpec spec = strain_machine(s);
        const Program p = program_from_json(spec, s.program);
        md << "\n## " << s.id << "\n\n" << (s.description.empty() ? "" : s.description + "\n\n");
        md << "Evolved on `" << s.problem.dump() << "` in " << s.generations << " generations.\n\n";
        const Strain* parent = nullptr;
        for (std::size_t j = 0; j < k; ++j)
            if (std::find(s.parents.begin(), s.parents.end(), chain[j].id) != s.parents.end()) parent = &chain[j];
        if (parent) {
            const MachineSpec ps = strain_machine(*parent);
            md << "Behaviour compared with its parent " << parent->id << ":\n\n```diff\n"
               << program_diff(ps, program_from_json(ps, parent->program), spec, p, s.num_outputs) << "```\n";
        } else {
            md << "```\n" << to_pseudocode(spec, p, s.num_outputs, true) << "```\n";
        }
        if (s.problem.contains("events")) md << "\n";
    }
    return md.str();
}

// ------------------------------------------------------------------ C++ export

namespace {

std::string identifier(const std::string& s) {
    std::string out;
    for (char c : s) out += std::isalnum(static_cast<unsigned char>(c)) ? c : '_';
    if (out.empty() || std::isdigit(static_cast<unsigned char>(out[0]))) out = "_" + out;
    return out;
}

std::string exact(double v) {  // hex float literal: the constant round-trips exactly
    char buf[64];
    std::snprintf(buf, sizeof buf, "%a", v);
    return buf;
}

struct Emitter {
    const MachineSpec& spec;
    const Program& p;
    std::ostringstream& out;

    std::size_t stmt(std::size_t i, int depth) {
        const std::size_t n = p.code.size();
        if (i >= n) return n;
        const Instr& in = p.code[i];
        const std::string ind(static_cast<std::size_t>(depth) * 4, ' ');
        auto R = [](int k) { return "r[" + std::to_string(k) + "]"; };
        const std::string d = R(in.dst), a = R(in.a), b = R(in.b);
        out << ind << "if (steps++ >= kMaxSteps) return;\n";
        switch (in.op) {
            case Op::Nop: break;
            case Op::Const: out << ind << d << " = " << exact(in.imm) << ";\n"; break;
            case Op::Mov: out << ind << d << " = " << a << ";\n"; break;
            case Op::Add: out << ind << d << " = clean(" << a << " + " << b << ");\n"; break;
            case Op::Sub: out << ind << d << " = clean(" << a << " - " << b << ");\n"; break;
            case Op::Mul: out << ind << d << " = clean(" << a << " * " << b << ");\n"; break;
            case Op::Div: out << ind << d << " = clean(pdiv(" << a << ", " << b << "));\n"; break;
            case Op::Min: out << ind << d << " = std::min(" << a << ", " << b << ");\n"; break;
            case Op::Max: out << ind << d << " = std::max(" << a << ", " << b << ");\n"; break;
            case Op::Neg: out << ind << d << " = -" << a << ";\n"; break;
            case Op::Abs: out << ind << d << " = std::fabs(" << a << ");\n"; break;
            case Op::Sin: out << ind << d << " = std::sin(" << a << ");\n"; break;
            case Op::Cos: out << ind << d << " = std::cos(" << a << ");\n"; break;
            case Op::Exp: out << ind << d << " = clean(std::exp(std::min(" << a << ", 50.0)));\n"; break;
            case Op::Log: out << ind << d << " = plog(" << a << ");\n"; break;
            case Op::Sqrt: out << ind << d << " = std::sqrt(std::fabs(" << a << "));\n"; break;
            case Op::IfLt:
            case Op::IfGe:
            case Op::IfEq: {
                const std::string cond = in.op == Op::IfLt ? a + " < " + b : in.op == Op::IfGe ? a + " >= " + b : "std::fabs(" + a + " - " + b + ") < 1e-9";
                if (i + 1 >= n) return n;
                out << ind << "if (" << cond << ") {\n";
                const std::size_t end = stmt(i + 1, depth + 1);
                out << ind << "}\n";
                return end;
            }
            case Op::Loop: {
                const int body = std::clamp<int>(in.c, 1, spec.limits.max_loop_body);
                out << ind << "{\n" << ind << "    const int count = static_cast<int>(std::min<double>(std::fabs(" << a
                    << "), kMaxLoop));\n"
                    << ind << "    for (int k = 0; k < count; ++k) {\n" << ind << "        " << d << " = k;\n";
                std::size_t j = i + 1;
                for (int s = 0; s < body && j < n; ++s) j = stmt(j, depth + 2);
                out << ind << "    }\n" << ind << "}\n";
                return j;
            }
            case Op::Sense:
            case Op::Act: {
                if (in.channel >= spec.channels.size()) break;
                const Channel& ch = spec.channels[in.channel];
                if ((ch.kind == ChannelKind::Sensor) != (in.op == Op::Sense)) break;
                out << ind << "{\n" << ind << "    const double args[3] = {" << a << ", " << b << ", " << R(in.c) << "};\n"
                    << ind << "    " << d << " = clean(env." << (in.op == Op::Sense ? "sense" : "act") << "("
                    << identifier(ch.name) << ", args, " << std::clamp(ch.arity, 0, 3) << "));\n"
                    << ind << "}\n";
                break;
            }
            case Op::Count_: break;
        }
        return i + 1;
    }
};

}  // namespace

std::string export_cpp(const MachineSpec& spec, const Program& program, int num_outputs, const std::string& name,
                       const std::string& comment) {
    const std::string ns = identifier(name);
    bool simple = true;  // only arity-0 sensors and no actions: offer a plain function of numbers
    for (const auto& c : spec.channels) simple = simple && c.kind == ChannelKind::Sensor && c.arity == 0;
    std::ostringstream out;
    out << "// Generated by adaptogen" << (comment.empty() ? "" : ": " + comment) << "\n"
        << "// Computes exactly what the adaptogen virtual machine computes for this evolved program\n"
        << "// (same protected operators, loop cap and step limit). Self-contained: no adaptogen needed.\n"
        << "#pragma once\n#include <algorithm>\n#include <cmath>\n\nnamespace " << ns << " {\n\n";
    out << "// Channels the program uses to observe (sense) and act.\n";
    for (std::size_t k = 0; k < spec.channels.size(); ++k) {
        const Channel& c = spec.channels[k];
        out << "constexpr int " << identifier(c.name) << " = " << k << ";  // " << to_string(c.kind) << ", " << c.arity
            << " argument(s)" << (c.units.empty() ? "" : " [" + c.units + "]") << (c.description.empty() ? "" : ": " + c.description) << "\n";
    }
    out << "\nconstexpr int kRegisters = " << spec.num_registers << ";\n"
        << "constexpr int kOutputs = " << num_outputs << ";  // the answer is in r[0.." << std::max(0, num_outputs - 1) << "]\n"
        << "constexpr int kMaxSteps = " << spec.limits.max_steps << ";\n"
        << "constexpr double kMaxLoop = " << spec.limits.max_loop_iterations << ";\n\n"
        << "/// Implement this to connect the program to your data or system.\n"
        << "struct Environment {\n    virtual ~Environment() = default;\n"
        << "    virtual double sense(int channel, const double* args, int nargs) = 0;\n"
        << "    virtual double act(int channel, const double* args, int nargs) { (void)channel; (void)args; (void)nargs; return 0.0; }\n};\n\n"
        << "namespace detail {\n"
        << "inline double clean(double v) { return std::isnan(v) ? 0.0 : std::clamp(v, -1e15, 1e15); }\n"
        << "inline double pdiv(double a, double b) { return std::fabs(b) < 1e-9 ? 1.0 : a / b; }\n"
        << "inline double plog(double a) { return std::fabs(a) < 1e-9 ? 0.0 : std::log(std::fabs(a)); }\n"
        << "}  // namespace detail\n\n"
        << "/// Run the program once. r holds kRegisters values (start them at 0); they are used as-is, so\n"
        << "/// calling run() repeatedly with the same r keeps the program's memory (e.g. per time step).\n"
        << "inline void run(Environment& env, double* r) {\n"
        << "    using detail::clean;\n    using detail::pdiv;\n    using detail::plog;\n    int steps = 0;\n"
        << "    (void)env;\n    (void)steps;\n";
    Emitter em{spec, program, out};
    for (std::size_t i = 0; i < program.code.size();) i = em.stmt(i, 1);
    out << "}\n";
    if (simple && num_outputs >= 1) {
        out << "\n/// Convenience: the program as a plain function of its inputs.\ninline double compute(";
        for (std::size_t k = 0; k < spec.channels.size(); ++k) out << (k ? ", " : "") << "double " << identifier(spec.channels[k].name);
        out << ") {\n    struct Inputs : Environment {\n        double v[" << std::max<std::size_t>(1, spec.channels.size()) << "];\n"
            << "        double sense(int channel, const double*, int) override { return v[channel]; }\n    } env;\n";
        for (std::size_t k = 0; k < spec.channels.size(); ++k) out << "    env.v[" << k << "] = " << identifier(spec.channels[k].name) << ";\n";
        out << "    double r[kRegisters] = {};\n    run(env, r);\n    return r[0];\n}\n";
    }
    out << "\n}  // namespace " << ns << "\n";
    return out.str();
}

}  // namespace adaptogen
