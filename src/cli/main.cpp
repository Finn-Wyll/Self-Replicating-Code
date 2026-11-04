// adaptogen command-line interface.
#include <cctype>
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
#include "adaptogen/decide.hpp"
#include "adaptogen/mcp.hpp"
#include "adaptogen/plugin.hpp"
#include "adaptogen/report.hpp"
#include "adaptogen/serialize.hpp"
#include "adaptogen/simplify.hpp"
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
  adaptogen plugin new|header|build|validate|info   write problems as C++ plug-ins (see below)
  adaptogen decide STRAIN --options JSON|@FILE [--context JSON|@FILE]
                                              apply a strain: choice, scores, confidence (JSON)
  adaptogen adapt STRAIN --data NEW.csv [--only-new] [--generations N] [--force]
                                              re-evolve a data-driven strain on old + new data (or only the
                                              new data: specialise it to your use case), starting from it;
                                              saved as the next version only if not worse on held-out
                                              cases (--force saves anyway)
  adaptogen mcp [--workspace DIR] [--library DIR]   run as an MCP server (tools for Claude) on stdio
  adaptogen export (--program FILE | --strain ID) [--language cpp|python|javascript] [--name NAME] [-o FILE]
                                              plug-in code for apps: exact run(), choose(options) and
                                              a report card against simple solutions

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
  --lineage FILE.md      write how the solution evolved: every improvement, what produced it
                         and the code change
  --no-simplify          keep the raw evolved program (default: remove code that does not
                         change any training case's score, verified case by case)

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
  adaptogen strains simplify ID                         save a simplified new version (verified)
  adaptogen strains lineage ID [-o FILE.md]             ancestry report: problems, scores, code changes

plug-ins (new problems in C++, run in isolated worker processes; use as --problem plugin:FILE.cpp):
  adaptogen plugin new NAME [--from STRAIN]... [--rename STRAIN:CHANNEL=NEW]... [--dir DIR]
                                   write NAME.cpp (and NAME_base.hpp when deriving from strains)
  adaptogen plugin header STRAIN... --class NAME [--rename STRAIN:CHANNEL=NEW]... [-o FILE]
                                   generate a C++ base class from strains (multiple inheritance)
  adaptogen plugin build FILE.cpp [--json]       compile (cached); structured diagnostics
  adaptogen plugin validate FILE [--params JSON] [--param K=V] [--json]
                                   build, load and check interface, inheritance and behaviour
  adaptogen plugin info FILE [--params JSON]     print the plug-in's interface
  evolve --seed-parents            start from the plug-in's parent strains (with its channel maps)
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
    std::string problem_name, out_path, csv_path, save_strain, strain_description, lineage_path;
    json params = json::object();
    EngineConfig cfg;
    cfg.seeding.library = default_library();
    json overrides = json::object();
    json seeding = json::object();
    bool json_out = false, quiet = false, show_all = false, do_simplify = true, seed_parents = false;

    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        auto next = [&]() -> const std::string& {
            if (i + 1 >= args.size()) throw std::runtime_error(a + " needs a value");
            return args[++i];
        };
        if (a == "--no-simplify") do_simplify = false;
        else if (a == "--problem") problem_name = next();
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
        else if (a == "--lineage") lineage_path = next();
        else if (a == "--description") strain_description = next();
        else if (a == "--seed-strain") seeding["strains"].push_back(next());
        else if (a == "--seed-parents") seed_parents = true;
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
    if (seed_parents) {
        auto* plugin = dynamic_cast<PluginProblem*>(problem.get());
        if (!plugin || plugin->parents().empty())
            throw std::runtime_error("--seed-parents needs a plug-in problem that derives from strains");
        for (const auto& p : plugin->parents()) cfg.seeding.strains.push_back(p);
        for (const auto& m : plugin->channel_maps()) cfg.seeding.channel_map.push_back(m);
    }
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
    EvolutionResult res = run_evolution(*problem, cfg, on_generation, &prepared);
    const std::size_t raw_length = res.best.size();
    const Program raw_program = res.best;
    if (do_simplify) {
        // Verify against what the run optimised (including retained parent problems).
        const auto sr = simplify(prepared.problem(*problem), spec, res.best);
        res.best = sr.program;
        const Problem& scored = prepared.problem(*problem);
        res.train_metrics = scored.metrics(spec, res.best, Split::Train);
        res.test_metrics = scored.metrics(spec, res.best, Split::Test);
        res.solved_train = problem->solved(spec, res.best, Split::Train);
        res.solved_test = problem->solved(spec, res.best, Split::Test);
    }

    const json problem_spec = {{"name", problem_name}, {"params", params}, {"seed", cfg.seed}};
    json saved = result_to_json(spec, res);
    saved["problem"] = problem_spec;
    saved["config"] = config_to_json(cfg);
    if (do_simplify) saved["raw_program"] = program_to_json(spec, raw_program);
    saved["lineage"] = json::array();
    for (const auto& e : res.lineage)
        saved["lineage"].push_back({{"generation", e.generation}, {"fitness", e.fitness}, {"origin", e.origin}});
    if (!lineage_path.empty()) {
        std::ofstream lf(lineage_path);
        if (!lf) throw std::runtime_error("cannot write '" + lineage_path + "'");
        lf << lineage_report(spec, problem->num_outputs(), res, problem->name());
    }
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
        if (do_simplify && raw_length != res.best.size())
            std::printf("simplified from %zu to %zu instructions (same score on every training case)\n", raw_length,
                        res.best.size());
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
    if (!lineage_path.empty() && !json_out)
        std::printf("lineage report (%zu improvements) written to %s\n", res.lineage.size(), lineage_path.c_str());
    return 0;
}

nlohmann::json diagnostics_json(const BuildResult& b) {
    json d = json::array();
    for (const auto& x : b.diagnostics)
        d.push_back({{"file", x.file}, {"line", x.line}, {"column", x.column}, {"severity", x.severity}, {"message", x.message}});
    return d;
}

int cmd_plugin(std::vector<std::string> args) {
    if (args.empty()) throw std::runtime_error("usage: adaptogen plugin new|header|build|validate|info ...");
    const std::string sub = args[0];
    args.erase(args.begin());
    std::string library = default_library(), class_name, out_file, dir = ".";
    std::vector<std::string> positional, from;
    std::vector<std::pair<std::string, std::string>> renames;
    json params = json::object();
    bool json_out = false;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        auto next = [&]() -> const std::string& {
            if (i + 1 >= args.size()) throw std::runtime_error(a + " needs a value");
            return args[++i];
        };
        if (a == "--library") library = next();
        else if (a == "--class") class_name = next();
        else if (a == "-o" || a == "--out") out_file = next();
        else if (a == "--dir") dir = next();
        else if (a == "--from") from.push_back(next());
        else if (a == "--json") json_out = true;
        else if (a == "--params") params.update(json::parse(next()));
        else if (a == "--param") { auto [k, v] = key_value(next()); params[k] = v; }
        else if (a == "--rename") {
            const std::string kv = next();
            const auto eq = kv.find('=');
            if (eq == std::string::npos || kv.find(':') > eq) throw std::runtime_error("--rename expects STRAIN:CHANNEL=NEW");
            renames.emplace_back(kv.substr(0, eq), kv.substr(eq + 1));
        } else if (!a.empty() && a[0] != '-') positional.push_back(a);
        else throw std::runtime_error("unknown option '" + a + "'");
    }
    auto camel = [](const std::string& s) {
        std::string out;
        bool up = true;
        for (char c : s) {
            if (!std::isalnum(static_cast<unsigned char>(c))) { up = true; continue; }
            out += up ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : c;
            up = false;
        }
        return out.empty() || std::isdigit(static_cast<unsigned char>(out[0])) ? "P" + out : out;
    };
    auto load_strains = [&](const std::vector<std::string>& ids) {
        StrainLibrary lib(library);
        std::vector<Strain> out;
        for (const auto& id : ids) out.push_back(lib.load(id));
        return out;
    };
    auto write_file = [](const std::filesystem::path& p, const std::string& text) {
        if (std::filesystem::exists(p)) throw std::runtime_error("'" + p.string() + "' already exists (not overwritten)");
        std::ofstream out(p);
        if (!out) throw std::runtime_error("cannot write '" + p.string() + "'");
        out << text;
    };

    if (sub == "header") {
        if (positional.empty()) throw std::runtime_error("plugin header needs at least one strain");
        if (class_name.empty()) throw std::runtime_error("plugin header needs --class NAME");
        const std::string text = generate_base_header(load_strains(positional), class_name, renames);
        if (out_file.empty()) std::cout << text;
        else {
            write_file(out_file, text);
            std::printf("wrote %s\n", out_file.c_str());
        }
        return 0;
    }
    if (sub == "new") {
        if (positional.size() != 1) throw std::runtime_error("plugin new needs exactly one NAME");
        const std::string name = positional[0];
        if (class_name.empty()) class_name = camel(name);
        std::filesystem::create_directories(dir);
        std::string header, base;
        if (!from.empty()) {
            base = class_name + "Base";
            header = name + "_base.hpp";
            write_file(std::filesystem::path(dir) / header, generate_base_header(load_strains(from), base, renames));
            std::printf("wrote %s/%s (base class %s from %zu strain(s))\n", dir.c_str(), header.c_str(), base.c_str(), from.size());
        }
        write_file(std::filesystem::path(dir) / (name + ".cpp"), plugin_template(name, class_name, header, base));
        std::printf("wrote %s/%s.cpp\nnext: edit it, then `adaptogen plugin validate %s/%s.cpp`\n", dir.c_str(), name.c_str(),
                    dir.c_str(), name.c_str());
        return 0;
    }
    if (positional.size() != 1) throw std::runtime_error("plugin " + sub + " needs exactly one FILE");
    const std::filesystem::path file = positional[0];
    if (sub == "build") {
        const BuildResult b = build_plugin(file);
        if (json_out) {
            std::cout << json{{"ok", b.ok}, {"library", b.library.string()}, {"cached", b.cached}, {"command", b.command},
                              {"diagnostics", diagnostics_json(b)}, {"output", b.output}}.dump(2) << '\n';
        } else if (b.ok) {
            std::printf("%s %s\n", b.cached ? "up to date:" : "built", b.library.string().c_str());
        } else {
            std::printf("build failed:\n%s", b.output.c_str());
        }
        return b.ok ? 0 : 1;
    }
    if (sub == "validate") {
        const ValidationReport r = validate_plugin(file, params, 1, library);
        if (json_out) {
            std::cout << json{{"ok", r.ok}, {"errors", r.errors}, {"warnings", r.warnings}, {"notes", r.notes},
                              {"info", r.info}, {"diagnostics", diagnostics_json(r.build)}}.dump(2) << '\n';
        } else {
            for (const auto& e : r.errors) std::printf("error: %s\n", e.c_str());
            for (const auto& w : r.warnings) std::printf("warning: %s\n", w.c_str());
            for (const auto& n : r.notes) std::printf("note: %s\n", n.c_str());
            std::printf("%s\n", r.ok ? "plug-in OK" : "plug-in has errors");
        }
        return r.ok ? 0 : 1;
    }
    if (sub == "info") {
        auto p = make_plugin_problem(file.string(), params, 1);
        std::cout << p->info().dump(2) << '\n';
        return 0;
    }
    throw std::runtime_error("unknown plugin subcommand '" + sub + "'");
}

json json_arg(const std::string& v) {  // JSON text, or @file
    if (!v.empty() && v[0] == '@') return read_json(v.substr(1));
    return json::parse(v);
}

int cmd_decide(const std::vector<std::string>& args) {
    std::string id, library = default_library();
    json options, context = json::object();
    for (std::size_t i = 0; i < args.size(); ++i) {
        auto next = [&]() -> const std::string& {
            if (i + 1 >= args.size()) throw std::runtime_error(args[i] + " needs a value");
            return args[++i];
        };
        if (args[i] == "--options") options = json_arg(next());
        else if (args[i] == "--context") context = json_arg(next());
        else if (args[i] == "--library") library = next();
        else if (id.empty() && args[i][0] != '-') id = args[i];
        else throw std::runtime_error("unknown option '" + args[i] + "'");
    }
    if (id.empty()) throw std::runtime_error("decide needs a strain id");
    const Strain s = StrainLibrary(library).load(id);
    json out = to_json(decide(s, options, context));
    out["strain"] = s.id;
    std::cout << out.dump(2) << '\n';
    return 0;
}

int cmd_adapt(const std::vector<std::string>& args) {
    std::string id, library = default_library();
    std::vector<std::string> data;
    int generations = 0;
    long long seed = -1;
    bool force = false, only_new = false;
    for (std::size_t i = 0; i < args.size(); ++i) {
        auto next = [&]() -> const std::string& {
            if (i + 1 >= args.size()) throw std::runtime_error(args[i] + " needs a value");
            return args[++i];
        };
        if (args[i] == "--only-new") only_new = true;
        else if (args[i] == "--seed") seed = std::stoll(next());
        else if (args[i] == "--data") data.push_back(next());
        else if (args[i] == "--generations") generations = std::stoi(next());
        else if (args[i] == "--library") library = next();
        else if (args[i] == "--force") force = true;
        else if (id.empty() && args[i][0] != '-') id = args[i];
        else throw std::runtime_error("unknown option '" + args[i] + "'");
    }
    if (id.empty() || data.empty()) throw std::runtime_error("usage: adaptogen adapt STRAIN --data NEW.csv");
    StrainLibrary lib(library);
    const Strain s = lib.load(id);
    json params = s.problem.value("params", json::object());
    if (!params.contains("data")) throw std::runtime_error("adapt needs a strain evolved from data (decisions or formula with data)");
    json files = only_new ? json::array() : (params.at("data").is_array() ? params.at("data") : json::array({params.at("data")}));
    for (const auto& d : data) files.push_back(d);
    params["data"] = s.problem.value("name", "") == "decisions" ? files : json(files.back());
    EngineConfig cfg;
    if (!s.config.empty()) config_from_json(cfg, s.config);
    cfg.seeding = EngineConfig::Seeding{};
    cfg.seeding.strains = {s.id};
    cfg.seeding.library = library;
    if (generations > 0) cfg.generations = generations;
    if (seed >= 0) cfg.seed = static_cast<std::uint64_t>(seed);
    const std::string pname = s.problem.at("name").get<std::string>();
    auto problem = make_problem(pname, params, cfg.seed);
    const MachineSpec spec = make_machine(*problem, cfg);
    const Program before = revive(s, spec);
    const double old_score = problem->mean_score(spec, before, Split::Test);
    std::printf("adapting %s on %zu training cases (old + new data)...\n", s.id.c_str(), problem->num_cases(Split::Train));
    PreparedRun prepared;
    EvolutionResult res = run_evolution(*problem, cfg, {}, &prepared);
    res.best = simplify(*problem, spec, res.best).program;
    res.train_metrics = problem->metrics(spec, res.best, Split::Train);
    res.test_metrics = problem->metrics(spec, res.best, Split::Test);
    res.solved_test = problem->solved(spec, res.best, Split::Test);
    const double new_score = problem->mean_score(spec, res.best, Split::Test);
    std::printf("held-out score: %s %.6g -> adapted %.6g\n  before: %s\n  after:  %s\n", s.id.c_str(), old_score, new_score,
                metrics_line(problem->metrics(spec, before, Split::Test)).c_str(), metrics_line(res.test_metrics).c_str());
    if (new_score + 1e-12 < old_score && !force) {
        std::printf("not saved: the adapted program is worse on held-out cases (use --force to save anyway)\n");
        return 0;
    }
    const std::string nid = lib.save(make_strain(s.name, s.description, *problem, spec, res,
                                                 {{"name", pname}, {"params", params}, {"seed", cfg.seed}}, cfg, {s.id}));
    std::printf("saved %s (parent %s)\n", nid.c_str(), s.id.c_str());
    return 0;
}

int cmd_export(const std::vector<std::string>& args) {
    std::string program_file, strain_id, name, out_file, library = default_library(), language = "cpp";
    for (std::size_t i = 0; i < args.size(); ++i) {
        auto next = [&]() -> const std::string& {
            if (i + 1 >= args.size()) throw std::runtime_error(args[i] + " needs a value");
            return args[++i];
        };
        if (args[i] == "--program") program_file = next();
        else if (args[i] == "--strain") strain_id = next();
        else if (args[i] == "--name") name = next();
        else if (args[i] == "--language") language = next();
        else if (args[i] == "-o") out_file = next();
        else if (args[i] == "--library") library = next();
        else throw std::runtime_error("unknown option '" + args[i] + "'");
    }
    std::string code;
    if (!strain_id.empty()) {
        const Strain s = StrainLibrary(library).load(strain_id);
        const MachineSpec spec = strain_machine(s);
        code = export_code(export_language(language), spec, program_from_json(spec, s.program), s.num_outputs,
                           name.empty() ? s.id : name, report_card(s), s.extras);
    } else if (!program_file.empty()) {
        const json saved = read_json(program_file);
        EngineConfig cfg;
        if (saved.contains("config")) config_from_json(cfg, saved.at("config"));
        const json& pj = saved.at("problem");
        auto problem = make_problem(pj.at("name").get<std::string>(), pj.value("params", json::object()), pj.value("seed", cfg.seed));
        const MachineSpec spec = make_machine(*problem, cfg);
        code = export_code(export_language(language), spec, program_from_json(spec, saved.at("program")), problem->num_outputs(),
                           name.empty() ? problem->name() : name, {"evolved for " + problem->name()});
    } else {
        throw std::runtime_error("export needs --program FILE or --strain ID");
    }
    if (out_file.empty()) std::cout << code;
    else {
        std::ofstream o(out_file);
        if (!o) throw std::runtime_error("cannot write '" + out_file + "'");
        o << code;
        std::printf("wrote %s\n", out_file.c_str());
    }
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
        else if (a == "-o") ++i;  // handled by the subcommand
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
    if (sub == "simplify") {
        if (!s.problem.contains("name")) throw std::runtime_error("strain " + s.id + " does not record its problem");
        register_builtin_problems();
        auto problem = make_problem(s.problem.at("name").get<std::string>(), s.problem.value("params", json::object()),
                                    s.problem.value("seed", std::uint64_t{1}));
        EngineConfig cfg;
        if (!s.config.empty()) config_from_json(cfg, s.config);
        const MachineSpec spec = make_machine(*problem, cfg);
        const Program before = program_from_json(spec, s.program);
        const auto sr = simplify(*problem, spec, before);
        EvolutionResult r;
        r.best = sr.program;
        r.generations = s.generations;
        r.program_evaluations = s.program_evaluations;
        r.train_metrics = problem->metrics(spec, r.best, Split::Train);
        r.test_metrics = problem->metrics(spec, r.best, Split::Test);
        r.solved_test = problem->solved(spec, r.best, Split::Test);
        Strain out = make_strain(s.name, s.description.empty() ? "simplified" : s.description + " (simplified)", *problem,
                                 spec, r, s.problem, cfg, {s.id});
        const std::string id2 = lib.save(out);
        std::printf("%s: %zu -> %zu instructions (%d edits, verified on %zu training cases); saved as %s\n",
                    s.id.c_str(), sr.length_before, sr.length_after, sr.accepted_changes,
                    problem->num_cases(Split::Train), id2.c_str());
        return 0;
    }
    if (sub == "lineage") {
        const std::string text = strain_lineage_report(lib, s.id);
        std::string out_file;
        for (std::size_t i = 0; i + 1 < args.size(); ++i)
            if (args[i] == "-o") out_file = args[i + 1];
        if (out_file.empty()) std::cout << text;
        else {
            std::ofstream o(out_file);
            o << text;
            std::printf("wrote %s\n", out_file.c_str());
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
        if (cmd == "plugin") return cmd_plugin(args);
        if (cmd == "export") return cmd_export(args);
        if (cmd == "decide") return cmd_decide(args);
        if (cmd == "adapt") return cmd_adapt(args);
        if (cmd == "mcp") {
            std::string workspace = ".", library = default_library();
            for (std::size_t i = 0; i + 1 < args.size(); i += 2) {
                if (args[i] == "--workspace") workspace = args[i + 1];
                else if (args[i] == "--library") library = args[i + 1];
                else throw std::runtime_error("unknown option '" + args[i] + "'");
            }
            if (!std::filesystem::is_directory(workspace)) throw std::runtime_error("workspace '" + workspace + "' is not a folder");
            McpServer server(workspace, std::filesystem::absolute(library));
            return server.serve(std::cin, std::cout);
        }
        std::cerr << "unknown command '" << cmd << "'\n\n" << kUsage;
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
        return 1;
    }
}
