// adaptogen command-line interface.
#include <cstdio>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "adaptogen/analysis.hpp"
#include "adaptogen/engine.hpp"
#include "adaptogen/registry.hpp"
#include "adaptogen/serialize.hpp"

using namespace adaptogen;
using nlohmann::json;

namespace {

const char* kUsage = R"(adaptogen - evolve programs that solve problems

usage:
  adaptogen list                              list problems and their parameters
  adaptogen config                            print the default engine config as JSON
  adaptogen evolve --problem NAME [options]   evolve a program
  adaptogen show --program FILE [--all]       show a saved program and its scores

evolve options:
  --params JSON          problem parameters, e.g. '{"target":"koza1"}'
  --param KEY=VALUE      set one problem parameter (repeatable)
  --config FILE          engine config JSON (see `adaptogen config`)
  --set KEY=VALUE        override one config key (repeatable), e.g. --set population=1000
  --seed N  --population N  --generations N  --threads N
  --selection tournament|lexicase
  --out FILE             save the result (program + problem + config) as JSON
  --csv FILE             write per-generation statistics as CSV
  --json                 machine-readable output: one JSON event per line
  --quiet                only print the final result
  --all                  also show non-effective code (introns)
)";

json parse_value(const std::string& s) {
    try {
        return json::parse(s);
    } catch (...) {
        return s;
    }
}

std::pair<std::string, json> key_value(const std::string& kv) {
    const auto eq = kv.find('=');
    if (eq == std::string::npos) throw std::runtime_error("expected KEY=VALUE, got '" + kv + "'");
    return {kv.substr(0, eq), parse_value(kv.substr(eq + 1))};
}

json read_json(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot open '" + path + "'");
    return json::parse(in);
}

std::string metrics_line(const std::vector<Metric>& ms) {
    std::string s;
    char buf[64];
    for (const auto& m : ms) {
        std::snprintf(buf, sizeof buf, "%s%s=%.6g", s.empty() ? "" : " ", m.name.c_str(), m.value);
        s += buf;
    }
    return s;
}

int cmd_list() {
    for (const auto& e : registered_problems())
        std::cout << e.name << "\n    " << e.description << "\n    params: " << e.params_help << "\n";
    return 0;
}

int cmd_evolve(const std::vector<std::string>& args) {
    std::string problem_name, out_path, csv_path;
    json params = json::object();
    EngineConfig cfg;
    json overrides = json::object();
    bool json_out = false, quiet = false, show_all = false;

    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        auto next = [&]() -> const std::string& {
            if (i + 1 >= args.size()) throw std::runtime_error(a + " needs a value");
            return args[++i];
        };
        if (a == "--problem") problem_name = next();
        else if (a == "--params") params.update(json::parse(next()));
        else if (a == "--param") { auto [k, v] = key_value(next()); params[k] = v; }
        else if (a == "--config") config_from_json(cfg, read_json(next()));
        else if (a == "--set") { auto [k, v] = key_value(next()); overrides[k] = v; }
        else if (a == "--seed") overrides["seed"] = std::stoull(next());
        else if (a == "--population") overrides["population"] = std::stoi(next());
        else if (a == "--generations") overrides["generations"] = std::stoi(next());
        else if (a == "--threads") overrides["threads"] = std::stoi(next());
        else if (a == "--selection") overrides["selection"] = next();
        else if (a == "--out") out_path = next();
        else if (a == "--csv") csv_path = next();
        else if (a == "--json") json_out = true;
        else if (a == "--quiet") quiet = true;
        else if (a == "--all") show_all = true;
        else throw std::runtime_error("unknown option '" + a + "'");
    }
    if (problem_name.empty()) throw std::runtime_error("--problem is required (see `adaptogen list`)");
    config_from_json(cfg, overrides);

    auto problem = make_problem(problem_name, params, cfg.seed);
    Engine engine(*problem, cfg);

    std::ofstream csv;
    if (!csv_path.empty()) {
        csv.open(csv_path);
        if (!csv) throw std::runtime_error("cannot write '" + csv_path + "'");
    }
    bool header_written = false;
    engine.on_generation([&](const GenerationReport& r) {
        if (csv.is_open()) {
            if (!header_written) {
                csv << "generation,program_evaluations,seconds,best_fitness,mean_fitness,best_length,best_effective_length,mean_length";
                for (const auto& m : r.best_train_metrics) csv << ",train_" << m.name;
                csv << '\n';
                header_written = true;
            }
            csv << r.generation << ',' << r.program_evaluations << ',' << r.seconds << ',' << r.best_fitness << ','
                << r.mean_fitness << ',' << r.best_length << ',' << r.best_effective_length << ',' << r.mean_length;
            for (const auto& m : r.best_train_metrics) csv << ',' << m.value;
            csv << '\n';
        }
        if (json_out) std::cout << report_to_json(r).dump() << std::endl;
        else if (!quiet) {
            std::printf("gen %4d  evals %8ld  %6.1fs  best len %3.0f (effective %3.0f)  train: %s\n", r.generation,
                        r.program_evaluations, r.seconds, r.best_length, r.best_effective_length,
                        metrics_line(r.best_train_metrics).c_str());
            std::fflush(stdout);
        }
    });

    if (!json_out && !quiet)
        std::printf("problem: %s\n  %s\nselection: %s, population %d, up to %d generations, seed %llu\n\n",
                    problem->name().c_str(), problem->description().c_str(), to_string(cfg.selection), cfg.population,
                    cfg.generations, static_cast<unsigned long long>(cfg.seed));

    const EvolutionResult res = engine.run();
    const MachineSpec& spec = engine.machine();

    json saved = result_to_json(spec, res);
    saved["problem"] = {{"name", problem_name}, {"params", params}, {"seed", cfg.seed}};
    saved["config"] = config_to_json(cfg);

    if (json_out) {
        std::cout << saved.dump() << std::endl;
    } else {
        std::printf("\n%s after %d generations (%ld program evaluations, %.1fs)\n",
                    res.solved_train ? "SOLVED" : "finished", res.generations, res.program_evaluations, res.seconds);
        std::printf("  train: %s\n  test (unseen): %s%s\n\n", metrics_line(res.train_metrics).c_str(),
                    metrics_line(res.test_metrics).c_str(), res.solved_test ? "  [solved]" : "");
        std::printf("evolved program (%zu instructions, %zu effective):\n%s", res.best.size(),
                    effective_length(spec, res.best, problem->num_outputs()),
                    to_pseudocode(spec, res.best, problem->num_outputs(), !show_all).c_str());
    }
    if (!out_path.empty()) {
        std::ofstream out(out_path);
        if (!out) throw std::runtime_error("cannot write '" + out_path + "'");
        out << saved.dump(2) << '\n';
        if (!json_out) std::printf("\nsaved to %s\n", out_path.c_str());
    }
    return 0;
}

int cmd_show(const std::vector<std::string>& args) {
    std::string path;
    bool show_all = false;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--program" && i + 1 < args.size()) path = args[++i];
        else if (args[i] == "--all") show_all = true;
        else throw std::runtime_error("unknown option '" + args[i] + "'");
    }
    if (path.empty()) throw std::runtime_error("--program FILE is required");
    const json saved = read_json(path);
    EngineConfig cfg;
    if (saved.contains("config")) config_from_json(cfg, saved.at("config"));
    const json& pj = saved.at("problem");
    auto problem = make_problem(pj.at("name").get<std::string>(), pj.value("params", json::object()),
                                pj.value("seed", cfg.seed));
    const MachineSpec spec = make_machine(*problem, cfg);
    const Program prog = program_from_json(spec, saved.contains("program") ? saved.at("program") : saved);
    std::printf("problem: %s\n  train: %s\n  test:  %s\n\n%s", problem->name().c_str(),
                metrics_line(problem->metrics(spec, prog, Split::Train)).c_str(),
                metrics_line(problem->metrics(spec, prog, Split::Test)).c_str(),
                to_pseudocode(spec, prog, problem->num_outputs(), !show_all).c_str());
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    register_builtin_problems();
    std::vector<std::string> args(argv + 1, argv + argc);
    if (args.empty() || args[0] == "-h" || args[0] == "--help" || args[0] == "help") {
        std::cout << kUsage;
        return args.empty() ? 1 : 0;
    }
    try {
        const std::string cmd = args[0];
        args.erase(args.begin());
        if (cmd == "list") return cmd_list();
        if (cmd == "config") {
            std::cout << config_to_json(EngineConfig{}).dump(2) << '\n';
            return 0;
        }
        if (cmd == "evolve") return cmd_evolve(args);
        if (cmd == "show") return cmd_show(args);
        std::cerr << "unknown command '" << cmd << "'\n\n" << kUsage;
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
        return 1;
    }
}
