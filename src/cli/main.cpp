// adaptogen command-line interface.
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "adaptogen/analysis.hpp"
#include "adaptogen/engine.hpp"
#include "adaptogen/registry.hpp"
#include "adaptogen/serialize.hpp"
#include "adaptogen/strain.hpp"

using namespace adaptogen;
using nlohmann::json;

namespace {

const char* kUsage = R"(adaptogen - evolve programs that solve problems

usage:
  adaptogen list                              list problems and their parameters
  adaptogen config                            print the default engine config as JSON
  adaptogen evolve --problem NAME [options]   evolve a program
  adaptogen show --program FILE [--all]       show a saved program and its scores
  adaptogen strains list|show|check|reindex   browse and inspect the strain bank (see below)

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

strain bank (saved programs that can be revived on new problems):
  --save-strain NAME     save the result as strain NAME-v<next version>
  --description TEXT     description stored with the saved strain
  --seed-strain ID       start from a saved strain (repeatable: several parents)
  --seed-random F        fraction of the population that starts random (default 0.1)
  --retain W             keep scoring on the parent strains' problems with weight W (0..1)
  --map OLD=NEW          map a strain channel name to a problem channel name (repeatable)
  --library DIR          strain library folder (default: $ADAPTOGEN_LIBRARY or ./strains)

  adaptogen strains list [--library DIR]
  adaptogen strains show ID [--all]                     interface, behaviour and lineage
  adaptogen strains check ID --problem NAME [--params JSON] [--param K=V] [--map OLD=NEW]
  adaptogen strains reindex [--library DIR]
)";

std::string default_library() {
    const char* env = std::getenv("ADAPTOGEN_LIBRARY");
    return env && *env ? env : "strains";
}

void print_compatibility(const std::string& id, const Compatibility& c) {
    auto join = [](const std::vector<std::string>& v) {
        std::string s;
        for (const auto& x : v) s += (s.empty() ? "" : ", ") + x;
        return s.empty() ? std::string("-") : s;
    };
    std::printf("strain %s: %.0f%% of the channels its code uses are available\n", id.c_str(), 100 * c.coverage());
    std::printf("  matched: %s\n  missing (code becomes pseudogenes): %s\n  new (available to evolution): %s\n",
                join(c.matched).c_str(), join(c.missing).c_str(), join(c.added).c_str());
    for (const auto& w : c.warnings) std::printf("  warning: %s\n", w.c_str());
    std::printf("  (* = used by the strain's effective code)\n");
}

nlohmann::json compatibility_json(const std::string& id, const Compatibility& c) {
    return nlohmann::json{{"strain", id},           {"coverage", c.coverage()}, {"matched", c.matched},
                          {"missing", c.missing},   {"added", c.added},         {"warnings", c.warnings}};
}

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
    std::string problem_name, out_path, csv_path, save_strain, strain_description;
    json params = json::object();
    EngineConfig cfg;
    cfg.seeding.library = default_library();
    json overrides = json::object();
    json seeding = json::object();
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
        else if (a == "--save-strain") save_strain = next();
        else if (a == "--description") strain_description = next();
        else if (a == "--seed-strain") seeding["strains"].push_back(next());
        else if (a == "--seed-random") seeding["random_fraction"] = std::stod(next());
        else if (a == "--retain") seeding["retain_weight"] = std::stod(next());
        else if (a == "--library") seeding["library"] = next();
        else if (a == "--map") {
            const std::string kv = next();
            const auto eq = kv.find('=');
            if (eq == std::string::npos) throw std::runtime_error("--map expects OLD=NEW");
            seeding["channel_map"][kv.substr(0, eq)] = kv.substr(eq + 1);
        } else throw std::runtime_error("unknown option '" + a + "'");
    }
    if (problem_name.empty()) throw std::runtime_error("--problem is required (see `adaptogen list`)");
    if (!seeding.empty()) overrides["seeding"] = seeding;
    config_from_json(cfg, overrides);
    if (!save_strain.empty() && !StrainLibrary::valid_name(save_strain))
        throw std::runtime_error("invalid strain name '" + save_strain +
                                 "': use lowercase letters, digits, '.', '_' or '-', not ending in -v<number>");

    auto problem = make_problem(problem_name, params, cfg.seed);
    const MachineSpec spec = make_machine(*problem, cfg);

    // Seeding: report how the strains' interfaces line up with this problem before running.
    if (!cfg.seeding.strains.empty()) {
        StrainLibrary lib(cfg.seeding.library);
        for (const auto& id : cfg.seeding.strains) {
            const Strain s = lib.load(id);
            const Compatibility c = check_compatibility(s, spec.channels, cfg.seeding.channel_map);
            if (json_out) {
                auto ev = compatibility_json(s.id, c);
                ev["event"] = "seeding";
                std::cout << ev.dump() << std::endl;
            } else if (!quiet) {
                print_compatibility(s.id, c);
            }
        }
        if (!json_out && !quiet) std::printf("\n");
    }

    std::ofstream csv;
    if (!csv_path.empty()) {
        csv.open(csv_path);
        if (!csv) throw std::runtime_error("cannot write '" + csv_path + "'");
    }
    bool header_written = false;
    auto on_generation = [&](const GenerationReport& r) {
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
    };

    if (!json_out && !quiet)
        std::printf("problem: %s\n  %s\nselection: %s, population %d, up to %d generations, seed %llu\n\n",
                    problem->name().c_str(), problem->description().c_str(), to_string(cfg.selection), cfg.population,
                    cfg.generations, static_cast<unsigned long long>(cfg.seed));

    PreparedRun prepared;
    const EvolutionResult res = run_evolution(*problem, cfg, on_generation, &prepared);

    const json problem_spec = {{"name", problem_name}, {"params", params}, {"seed", cfg.seed}};
    json saved = result_to_json(spec, res);
    saved["problem"] = problem_spec;
    saved["config"] = config_to_json(cfg);
    std::string strain_id;
    if (!save_strain.empty()) {
        std::vector<std::string> parents;
        for (const auto& s : prepared.strains) parents.push_back(s.id);
        StrainLibrary lib(cfg.seeding.library);
        strain_id = lib.save(make_strain(save_strain, strain_description, *problem, spec, res, problem_spec, cfg, parents));
        saved["strain"] = strain_id;
    }

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
    if (!strain_id.empty() && !json_out)
        std::printf("\nsaved strain %s in %s/\n", strain_id.c_str(), cfg.seeding.library.c_str());
    return 0;
}

int cmd_strains(std::vector<std::string> args) {
    if (args.empty()) throw std::runtime_error("usage: adaptogen strains list|show|check|reindex ...");
    const std::string sub = args[0];
    args.erase(args.begin());
    std::string library = default_library(), id, problem_name;
    json params = json::object();
    ChannelMap map;
    bool show_all = false;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        auto next = [&]() -> const std::string& {
            if (i + 1 >= args.size()) throw std::runtime_error(a + " needs a value");
            return args[++i];
        };
        if (a == "--library") library = next();
        else if (a == "--problem") problem_name = next();
        else if (a == "--params") params.update(json::parse(next()));
        else if (a == "--param") { auto [k, v] = key_value(next()); params[k] = v; }
        else if (a == "--map") {
            const std::string kv = next();
            const auto eq = kv.find('=');
            if (eq == std::string::npos) throw std::runtime_error("--map expects OLD=NEW");
            map.emplace_back(kv.substr(0, eq), kv.substr(eq + 1));
        } else if (a == "--all") show_all = true;
        else if (!a.empty() && a[0] != '-' && id.empty()) id = a;
        else throw std::runtime_error("unknown option '" + a + "'");
    }
    StrainLibrary lib(library);

    if (sub == "reindex") {
        lib.reindex();
        std::printf("indexed %zu strains in %s/\n", lib.list().size(), library.c_str());
        return 0;
    }
    if (sub == "list") {
        const auto entries = lib.list();
        if (entries.empty()) std::printf("no strains in %s/ (save one with evolve --save-strain NAME)\n", library.c_str());
        for (const auto& e : entries) {
            std::string chans, parents, metrics;
            for (const auto& c : e.channels) chans += (chans.empty() ? "" : ", ") + c;
            for (const auto& p : e.parents) parents += (parents.empty() ? "" : ", ") + p;
            for (const auto& [k, v] : e.test_metrics.items()) {
                char buf[64];
                std::snprintf(buf, sizeof buf, "%s%s=%.4g", metrics.empty() ? "" : " ", k.c_str(), v.get<double>());
                metrics += buf;
            }
            std::printf("%-24s %s%s\n    problem: %s\n    channels: %s\n    test: %s%s\n", e.id.c_str(),
                        e.description.c_str(), e.solved_test ? "  [solved]" : "", e.problem.c_str(), chans.c_str(),
                        metrics.c_str(), parents.empty() ? "" : ("\n    parents: " + parents).c_str());
        }
        return 0;
    }
    if (id.empty()) throw std::runtime_error("strains " + sub + " needs a strain id or name");
    const Strain s = lib.load(id);
    if (sub == "show") {
        std::printf("strain %s  (created %s)\n  %s\n\ninterface:\n", s.id.c_str(), s.created.c_str(), s.description.c_str());
        for (std::size_t k = 0; k < s.channels.size(); ++k) {
            const Channel& c = s.channels[k];
            std::printf("  %-7s %-22s arity %d%s  %s%s\n", to_string(c.kind), c.name.c_str(), c.arity,
                        c.units.empty() ? "" : (" [" + c.units + "]").c_str(), c.description.c_str(),
                        k < s.channel_used.size() && s.channel_used[k] ? "   (used)" : "");
        }
        std::printf("  outputs: %d register(s), %d registers in total\n", s.num_outputs, s.num_registers);
        // Rebuild a machine from the stored interface to print the behaviour.
        MachineSpec spec;
        spec.num_registers = s.num_registers;
        spec.ops = s.ops;
        spec.channels = s.channels;
        spec.finalize();
        const Program prog = program_from_json(spec, s.program);
        std::printf("\nbehaviour (%zu instructions, %zu effective):\n%s", prog.size(),
                    effective_length(spec, prog, s.num_outputs), to_pseudocode(spec, prog, s.num_outputs, !show_all).c_str());
        std::printf("\nlineage:\n  problem: %s\n  %d generations, %ld program evaluations\n  test: %s%s\n",
                    s.problem.dump().c_str(), s.generations, s.program_evaluations, s.test_metrics.dump().c_str(),
                    s.solved_test ? "  [solved]" : "");
        const auto anc = lib.ancestry(s.id);
        if (anc.size() > 1) {
            std::printf("  ancestry:\n");
            for (std::size_t k = 1; k < anc.size(); ++k)
                std::printf("    %s  (%s)\n", anc[k].id.c_str(), anc[k].problem.value("name", "?").c_str());
        }
        return 0;
    }
    if (sub == "check") {
        if (problem_name.empty()) throw std::runtime_error("strains check needs --problem NAME");
        auto problem = make_problem(problem_name, params, 1);
        print_compatibility(s.id, check_compatibility(s, problem->channels(), map));
        return 0;
    }
    throw std::runtime_error("unknown strains subcommand '" + sub + "'");
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
        if (cmd == "strains") return cmd_strains(args);
        std::cerr << "unknown command '" << cmd << "'\n\n" << kUsage;
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
        return 1;
    }
}
