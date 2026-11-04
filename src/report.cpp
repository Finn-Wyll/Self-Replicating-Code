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
    out << "// Generated by adaptogen\n";
    {
        std::istringstream cl(comment);
        for (std::string l; std::getline(cl, l);) out << "// " << l << "\n";
    }
    out << "// Computes exactly what the adaptogen virtual machine computes for this evolved program\n"
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

// ------------------------------------------------------------------ Python / JavaScript export

ExportLanguage export_language(const std::string& n) {
    if (n == "cpp" || n == "c++" || n == "hpp") return ExportLanguage::Cpp;
    if (n == "python" || n == "py") return ExportLanguage::Python;
    if (n == "javascript" || n == "js" || n == "typescript" || n == "ts" || n == "mjs") return ExportLanguage::JavaScript;
    throw std::runtime_error("unknown language '" + n + "' (cpp, python, javascript)");
}

const char* file_extension(ExportLanguage lang) {
    switch (lang) {
        case ExportLanguage::Cpp: return ".hpp";
        case ExportLanguage::Python: return ".py";
        case ExportLanguage::JavaScript: return ".mjs";
    }
    return "";
}

namespace {

std::string literal17(double v) {  // %.17g round-trips every double exactly
    char buf[40];
    std::snprintf(buf, sizeof buf, "%.17g", v);
    std::string s = buf;
    if (s.find_first_of(".eEn") == std::string::npos) s += ".0";
    return s;
}

// Emits statements for Python (py=true) or JavaScript.
struct ScriptEmitter {
    const MachineSpec& spec;
    const Program& p;
    bool py;
    std::ostringstream& out;

    std::string R(int k) const { return "r[" + std::to_string(k) + "]"; }
    void line(int depth, const std::string& s) { out << std::string(static_cast<std::size_t>(depth) * (py ? 4 : 2), ' ') << s << (py ? "" : ";") << "\n"; }
    void open(int depth, const std::string& head) { out << std::string(static_cast<std::size_t>(depth) * (py ? 4 : 2), ' ') << head << (py ? ":" : " {") << "\n"; }
    void close(int depth) {
        if (!py) out << std::string(static_cast<std::size_t>(depth) * 2, ' ') << "}\n";
    }

    std::size_t stmt(std::size_t i, int depth) {
        const std::size_t n = p.code.size();
        if (i >= n) return n;
        const Instr& in = p.code[i];
        const std::string d = R(in.dst), a = R(in.a), b = R(in.b);
        const std::string abs = py ? "abs" : "Math.abs", mn = py ? "min" : "Math.min", mx = py ? "max" : "Math.max";
        const std::string M = py ? "math." : "Math.";
        if (py) {
            open(depth, "if steps >= MAX_STEPS");
            line(depth + 1, "return");
            line(depth, "steps += 1");
        } else {
            line(depth, "if (steps++ >= MAX_STEPS) return");
        }
        switch (in.op) {
            case Op::Nop: break;
            case Op::Const: line(depth, d + " = " + literal17(in.imm)); break;
            case Op::Mov: line(depth, d + " = " + a); break;
            case Op::Add: line(depth, d + " = _clean(" + a + " + " + b + ")"); break;
            case Op::Sub: line(depth, d + " = _clean(" + a + " - " + b + ")"); break;
            case Op::Mul: line(depth, d + " = _clean(" + a + " * " + b + ")"); break;
            case Op::Div: line(depth, d + " = _clean(_pdiv(" + a + ", " + b + "))"); break;
            case Op::Min: line(depth, d + " = " + mn + "(" + a + ", " + b + ")"); break;
            case Op::Max: line(depth, d + " = " + mx + "(" + a + ", " + b + ")"); break;
            case Op::Neg: line(depth, d + " = -" + a); break;
            case Op::Abs: line(depth, d + " = " + abs + "(" + a + ")"); break;
            case Op::Sin: line(depth, d + " = " + M + "sin(" + a + ")"); break;
            case Op::Cos: line(depth, d + " = " + M + "cos(" + a + ")"); break;
            case Op::Exp: line(depth, d + " = _clean(" + M + "exp(" + mn + "(" + a + ", 50.0)))"); break;
            case Op::Log: line(depth, d + " = _plog(" + a + ")"); break;
            case Op::Sqrt: line(depth, d + " = " + M + "sqrt(" + abs + "(" + a + "))"); break;
            case Op::IfLt:
            case Op::IfGe:
            case Op::IfEq: {
                if (i + 1 >= n) return n;
                const std::string cond = in.op == Op::IfLt ? a + " < " + b : in.op == Op::IfGe ? a + " >= " + b : abs + "(" + a + " - " + b + ") < 1e-9";
                open(depth, py ? "if " + cond : "if (" + cond + ")");
                const std::size_t end = stmt(i + 1, depth + 1);
                close(depth);
                return end;
            }
            case Op::Loop: {
                const int body = std::clamp<int>(in.c, 1, spec.limits.max_loop_body);
                const std::string k = "k" + std::to_string(depth);
                if (py) {
                    line(depth, "count = int(min(abs(" + a + "), MAX_LOOP))");
                    open(depth, "for " + k + " in range(count)");
                    line(depth + 1, d + " = float(" + k + ")");
                } else {
                    open(depth, "for (let " + k + " = 0, n" + k + " = Math.trunc(Math.min(Math.abs(" + a + "), MAX_LOOP)); " + k + " < n" + k + "; ++" + k + ")");
                    line(depth + 1, d + " = " + k);
                }
                std::size_t j = i + 1;
                for (int s = 0; s < body && j < n; ++s) j = stmt(j, depth + 1);
                close(depth);
                return j;
            }
            case Op::Sense:
            case Op::Act: {
                if (in.channel >= spec.channels.size()) break;
                const Channel& ch = spec.channels[in.channel];
                if ((ch.kind == ChannelKind::Sensor) != (in.op == Op::Sense)) break;
                const std::string args = "[" + a + ", " + b + ", " + R(in.c) + "]";
                const std::string fn = in.op == Op::Sense ? "sense" : "act";
                line(depth, d + " = _clean(" + fn + "(" + std::to_string(in.channel) + ", " + args + ", " + std::to_string(std::clamp(ch.arity, 0, 3)) + "))");
                break;
            }
            case Op::Count_: break;
        }
        return i + 1;
    }
};

}  // namespace

std::vector<std::string> report_card(const Strain& s) {
    std::vector<std::string> lines;
    lines.push_back("Strain " + s.id + (s.description.empty() ? "" : ": " + s.description));
    lines.push_back("Evolved on " + s.problem.value("name", std::string("?")) + "; lineage: " +
                    (s.parents.empty() ? std::string("no parents") : "parents " + s.parents.front()));
    std::string held = "Held-out (unseen) results:";
    for (const auto& [k, v] : s.test_metrics.items()) {
        char buf[96];
        std::snprintf(buf, sizeof buf, " %s=%.4g", k.c_str(), v.get<double>());
        held += buf;
    }
    lines.push_back(held);
    const auto& tm = s.test_metrics;
    if (tm.contains("accuracy") && tm.contains("linear_model_accuracy") && tm.contains("best_single_feature_accuracy")) {
        const double mine = tm.at("accuracy").get<double>(), lin = tm.at("linear_model_accuracy").get<double>(),
                     sf = tm.at("best_single_feature_accuracy").get<double>();
        std::string sfname = s.extras.value("decision", nlohmann::json::object()).value("simple_solutions", nlohmann::json::object()).value("best_single_feature", std::string("best single feature"));
        char buf[256];
        std::snprintf(buf, sizeof buf, "Picks a best option %.1f%% of the time vs %.1f%% for a linear model and %.1f%% for '%s'%s.",
                      100 * mine, 100 * lin, 100 * sf, sfname.c_str(),
                      mine > std::max(lin, sf) ? ": better than both simple solutions" : ": NOT better than the simple solutions; prefer them");
        lines.push_back(buf);
    }
    return lines;
}

std::string export_code(ExportLanguage lang, const MachineSpec& spec, const Program& program, int num_outputs,
                        const std::string& name, const std::vector<std::string>& header, const nlohmann::json& extras) {
    std::string head;
    for (const auto& h : header) head += h + "\n";
    if (lang == ExportLanguage::Cpp) return export_cpp(spec, program, num_outputs, name, head.empty() ? "" : head.substr(0, head.size() - 1));
    const bool py = lang == ExportLanguage::Python;
    const std::string c = py ? "# " : "// ";
    bool plain = true;  // only arity-0 sensors and no actions
    for (const auto& ch : spec.channels) plain = plain && ch.kind == ChannelKind::Sensor && ch.arity == 0;
    std::ostringstream out;
    out << c << "Generated by adaptogen. Computes exactly what the adaptogen virtual machine computes for\n"
        << c << "this evolved program (same protected operators, loop cap and step limit). No dependencies.\n";
    for (const auto& h : header) out << c << h << "\n";
    out << (py ? "\nimport math\n\n" : "\n");
    // channel table
    out << (py ? "CHANNELS = {\n" : "export const CHANNELS = {\n");
    for (std::size_t k = 0; k < spec.channels.size(); ++k) {
        const Channel& ch = spec.channels[k];
        out << "    \"" << ch.name << "\": " << k << ",  " << c << to_string(ch.kind) << ", " << ch.arity << " argument(s)"
            << (ch.units.empty() ? "" : " [" + ch.units + "]") << (ch.description.empty() ? "" : ": " + ch.description) << "\n";
    }
    out << "}" << (py ? "" : ";") << "\n";
    const std::string kw = py ? "" : "export const ";
    out << kw << "REGISTERS = " << spec.num_registers << (py ? "" : ";") << "\n"
        << kw << "OUTPUTS = " << num_outputs << (py ? "" : ";") << "\n"
        << kw << "MAX_STEPS = " << spec.limits.max_steps << (py ? "" : ";") << "\n"
        << kw << "MAX_LOOP = " << spec.limits.max_loop_iterations << (py ? "" : ";") << "\n";
    const auto& dec = extras.value("decision", nlohmann::json::object());
    const auto calib = dec.value("calibration", nlohmann::json::array());
    out << kw << "CALIBRATION = [";
    for (std::size_t i = 0; i < calib.size(); ++i)
        out << (i ? ", " : "") << "[" << literal17(calib[i].value("min_margin", 0.0)) << ", " << literal17(calib[i].value("accuracy", 0.0)) << "]";
    out << "]" << (py ? "  # (minimum margin, chance the choice is a best option)" : ";  // [minimum margin, chance the choice is a best option]") << "\n\n";
    if (py) {
        out << "def _clean(v):\n    if v != v:\n        return 0.0\n    return -1e15 if v < -1e15 else (1e15 if v > 1e15 else v)\n\n\n"
            << "def _pdiv(a, b):\n    return 1.0 if abs(b) < 1e-9 else a / b\n\n\n"
            << "def _plog(a):\n    return 0.0 if abs(a) < 1e-9 else math.log(abs(a))\n\n\n"
            << "def run(sense, r, act=None):\n"
            << "    \"\"\"Run the program once. sense(channel, args, nargs) -> float answers sensor reads; act(...)\n"
            << "    performs actions. r is a list of REGISTERS floats (start at 0.0), updated in place.\"\"\"\n"
            << "    if act is None:\n        act = lambda channel, args, nargs: 0.0  # noqa: E731\n"
            << "    steps = 0\n";
    } else {
        out << "function _clean(v) {\n  if (Number.isNaN(v)) return 0;\n  return v < -1e15 ? -1e15 : v > 1e15 ? 1e15 : v;\n}\n"
            << "function _pdiv(a, b) {\n  return Math.abs(b) < 1e-9 ? 1 : a / b;\n}\n"
            << "function _plog(a) {\n  return Math.abs(a) < 1e-9 ? 0 : Math.log(Math.abs(a));\n}\n\n"
            << "/**\n * Run the program once. sense(channel, args, nargs) answers sensor reads; act(...) performs actions.\n"
            << " * @param {(channel: number, args: number[], nargs: number) => number} sense\n"
            << " * @param {number[]} r  REGISTERS numbers (start at 0), updated in place\n"
            << " * @param {(channel: number, args: number[], nargs: number) => number} [act]\n */\n"
            << "export function run(sense, r, act = () => 0) {\n  let steps = 0;\n";
    }
    ScriptEmitter em{spec, program, py, out};
    std::size_t emitted = 0;
    for (std::size_t i = 0; i < program.code.size();) {
        i = em.stmt(i, 1);
        ++emitted;
    }
    if (py) out << "    return\n\n\n";
    else out << "}\n\n";
    if (plain) {
        // choose(): score each option from its values (by channel name, with or without prefix).
        if (py) {
            out << "def _values(option, context, options):\n"
                << "    v = [0.0] * len(CHANNELS)\n"
                << "    for source, prefixes in ((option, (\"\", \"option.\", \"item.\")), (context or {}, (\"\", \"context.\"))):\n"
                << "        for key, value in source.items():\n"
                << "            for p in prefixes:\n"
                << "                if p + key in CHANNELS:\n"
                << "                    v[CHANNELS[p + key]] = float(value)\n"
                << "                    break\n"
                << "    for name, idx in CHANNELS.items():\n"
                << "        if name in (\"options.count\", \"items.remaining\"):\n"
                << "            v[idx] = float(len(options))\n"
                << "        elif name.startswith(\"options.\") and name[-4:] in (\".min\", \".max\"):\n"
                << "            feat = name[8:-4]\n"
                << "            xs = [float(o.get(feat, o.get(\"option.\" + feat, 0.0))) for o in options]\n"
                << "            v[idx] = min(xs) if name.endswith(\".min\") else max(xs)\n"
                << "    return v\n\n\n"
                << "def score(option, context=None, options=None):\n"
                << "    \"\"\"The program's score for one option (higher = preferred).\"\"\"\n"
                << "    v = _values(option, context, options or [option])\n"
                << "    r = [0.0] * REGISTERS\n"
                << "    run(lambda channel, args, nargs: v[channel], r)\n"
                << "    return r[0]\n\n\n"
                << "def choose(options, context=None):\n"
                << "    \"\"\"Pick the best of several options (dicts of feature -> number). Returns a dict with the\n"
                << "    chosen index, every option's score, the margin and, when calibrated, the confidence.\"\"\"\n"
                << "    scores = [score(o, context, options) for o in options]\n"
                << "    order = sorted(range(len(scores)), key=lambda i: -scores[i])\n"
                << "    spread = scores[order[0]] - scores[order[-1]]\n"
                << "    margin = (scores[order[0]] - scores[order[1]]) / spread if len(scores) > 1 and spread > 0 else (1.0 if len(scores) < 2 else 0.0)\n"
                << "    confidence = None\n"
                << "    for min_margin, accuracy in CALIBRATION:\n"
                << "        if margin >= min_margin - 1e-12 or confidence is None:\n"
                << "            confidence = accuracy\n"
                << "    return {\"choice\": order[0], \"scores\": scores, \"ranking\": order, \"margin\": margin, \"confidence\": confidence}\n";
        } else {
            out << "function _values(option, context, options) {\n"
                << "  const v = new Array(Object.keys(CHANNELS).length).fill(0);\n"
                << "  for (const [source, prefixes] of [[option, ['', 'option.', 'item.']], [context || {}, ['', 'context.']]]) {\n"
                << "    for (const [key, value] of Object.entries(source)) {\n"
                << "      const p = prefixes.find((q) => (q + key) in CHANNELS);\n"
                << "      if (p !== undefined) v[CHANNELS[p + key]] = Number(value);\n"
                << "    }\n  }\n"
                << "  for (const [name, idx] of Object.entries(CHANNELS)) {\n"
                << "    if (name === 'options.count' || name === 'items.remaining') v[idx] = options.length;\n"
                << "    else if (name.startsWith('options.') && (name.endsWith('.min') || name.endsWith('.max'))) {\n"
                << "      const feat = name.slice(8, -4);\n"
                << "      const xs = options.map((o) => Number(o[feat] ?? o['option.' + feat] ?? 0));\n"
                << "      v[idx] = name.endsWith('.min') ? Math.min(...xs) : Math.max(...xs);\n"
                << "    }\n  }\n  return v;\n}\n\n"
                << "/** The program's score for one option (higher = preferred). */\n"
                << "export function score(option, context = {}, options = [option]) {\n"
                << "  const v = _values(option, context, options);\n"
                << "  const r = new Array(REGISTERS).fill(0);\n"
                << "  run((channel) => v[channel], r);\n  return r[0];\n}\n\n"
                << "/**\n * Pick the best of several options (objects of feature -> number).\n"
                << " * @returns {{choice: number, scores: number[], ranking: number[], margin: number, confidence: number|null}}\n */\n"
                << "export function choose(options, context = {}) {\n"
                << "  const scores = options.map((o) => score(o, context, options));\n"
                << "  const order = scores.map((_, i) => i).sort((a, b) => scores[b] - scores[a] || a - b);\n"
                << "  const spread = scores[order[0]] - scores[order[order.length - 1]];\n"
                << "  const margin = scores.length < 2 ? 1 : spread > 0 ? (scores[order[0]] - scores[order[1]]) / spread : 0;\n"
                << "  let confidence = null;\n"
                << "  for (const [minMargin, accuracy] of CALIBRATION) if (margin >= minMargin - 1e-12 || confidence === null) confidence = accuracy;\n"
                << "  return { choice: order[0], scores, ranking: order, margin, confidence };\n}\n";
        }
    }
    (void)emitted;
    return out.str();
}

}  // namespace adaptogen
