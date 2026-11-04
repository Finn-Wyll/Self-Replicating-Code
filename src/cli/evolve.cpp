// adaptogen list | config | evolve | show
#include <cstdio>
#include <fstream>
#include <iostream>

#include "adaptogen/analysis.hpp"
#include "adaptogen/engine.hpp"
#include "adaptogen/registry.hpp"
#include "adaptogen/report.hpp"
#include "adaptogen/serialize.hpp"
#include "common.hpp"

namespace adaptogen::cli {

int cmd_list(const Args&) {
    for (const auto& e : registered_problems())
        std::cout << e.name << "\n    " << e.description << "\n    params: " << e.params_help << "\n";
    return 0;
}

int cmd_config(const Args&) {
    std::cout << config_to_json(EngineConfig{}).dump(2) << '\n';
    return 0;
}

namespace {

// Per-generation output: CSV statistics, JSON events or a progress line.
struct Progress {
    std::ofstream csv;
    bool header_written = false, json_out = false, quiet = false;

    void operator()(const GenerationReport& r) {
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
    }
};

}  // namespace

int cmd_evolve(const Args& args) {
    std::string problem_name, out_path, csv_path, save_strain, strain_description, lineage_path;
    json params = json::object(), overrides = json::object(), seeding = json::object();
    EngineConfig cfg;
    cfg.seeding.library = default_library();
    bool show_all = false, do_simplify = true, seed_parents = false;
    Progress progress;

    Options()
        .value({"--problem"}, [&](const std::string& v) { problem_name = v; })
        .value({"--params"}, [&](const std::string& v) { params.update(json::parse(v)); })
        .value({"--param"}, [&](const std::string& v) { auto [k, x] = key_value(v); params[k] = x; })
        .value({"--config"}, [&](const std::string& v) { config_from_json(cfg, read_json(v)); })
        .value({"--set"}, [&](const std::string& v) { auto [k, x] = key_value(v); overrides[k] = x; })
        .value({"--seed"}, [&](const std::string& v) { overrides["seed"] = std::stoull(v); })
        .value({"--population"}, [&](const std::string& v) { overrides["population"] = std::stoi(v); })
        .value({"--generations"}, [&](const std::string& v) { overrides["generations"] = std::stoi(v); })
        .value({"--threads"}, [&](const std::string& v) { overrides["threads"] = std::stoi(v); })
        .value({"--selection"}, [&](const std::string& v) { overrides["selection"] = v; })
        .value({"--out"}, [&](const std::string& v) { out_path = v; })
        .value({"--csv"}, [&](const std::string& v) { csv_path = v; })
        .value({"--lineage"}, [&](const std::string& v) { lineage_path = v; })
        .value({"--save-strain"}, [&](const std::string& v) { save_strain = v; })
        .value({"--description"}, [&](const std::string& v) { strain_description = v; })
        .value({"--seed-strain"}, [&](const std::string& v) { seeding["strains"].push_back(v); })
        .value({"--seed-random"}, [&](const std::string& v) { seeding["random_fraction"] = std::stod(v); })
        .value({"--retain"}, [&](const std::string& v) { seeding["retain_weight"] = std::stod(v); })
        .value({"--library"}, [&](const std::string& v) { seeding["library"] = v; })
        .value({"--map"}, [&](const std::string& v) { auto [from, to] = mapping(v, "--map"); seeding["channel_map"][from] = to; })
        .flag({"--json"}, [&] { progress.json_out = true; })
        .flag({"--quiet"}, [&] { progress.quiet = true; })
        .flag({"--all"}, [&] { show_all = true; })
        .flag({"--no-simplify"}, [&] { do_simplify = false; })
        .flag({"--seed-parents"}, [&] { seed_parents = true; })
        .parse(args);
    if (problem_name.empty()) throw std::runtime_error("--problem is required (see `adaptogen list`)");
    if (!seeding.empty()) overrides["seeding"] = seeding;
    config_from_json(cfg, overrides);
    if (!save_strain.empty() && !StrainLibrary::valid_name(save_strain))
        throw std::runtime_error("invalid strain name '" + save_strain +
                                 "': use lowercase letters, digits, '.', '_' or '-', not ending in -v<number>");
    const bool json_out = progress.json_out, quiet = progress.quiet;

    auto problem = make_problem(problem_name, params, cfg.seed);
    if (seed_parents) seed_from_parents(*problem, cfg);
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

    if (!csv_path.empty()) {
        progress.csv.open(csv_path);
        if (!progress.csv) throw std::runtime_error("cannot write '" + csv_path + "'");
    }
    if (!json_out && !quiet)
        std::printf("problem: %s\n  %s\nselection: %s, population %d, up to %d generations, seed %llu\n\n",
                    problem->name().c_str(), problem->description().c_str(), to_string(cfg.selection), cfg.population,
                    cfg.generations, static_cast<unsigned long long>(cfg.seed));

    PreparedRun prepared;
    EvolutionResult res = run_evolution(*problem, cfg, std::ref(progress), &prepared);
    const Program raw_program = res.best;
    if (do_simplify) finish_result(prepared.problem(*problem), *problem, spec, res);

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
        strain_id = StrainLibrary(cfg.seeding.library)
                        .save(make_strain(save_strain, strain_description, *problem, spec, res, problem_spec, cfg, parents));
        saved["strain"] = strain_id;
    }

    if (json_out) {
        std::cout << saved.dump() << std::endl;
    } else {
        std::printf("\n%s after %d generations (%ld program evaluations, %.1fs)\n",
                    res.solved_train ? "SOLVED" : "finished", res.generations, res.program_evaluations, res.seconds);
        std::printf("  train: %s\n  test (unseen): %s%s\n\n", metrics_line(res.train_metrics).c_str(),
                    metrics_line(res.test_metrics).c_str(), res.solved_test ? "  [solved]" : "");
        if (do_simplify && raw_program.size() != res.best.size())
            std::printf("simplified from %zu to %zu instructions (same score on every training case)\n", raw_program.size(),
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

int cmd_show(const Args& args) {
    std::string path;
    bool show_all = false;
    Options()
        .value({"--program"}, [&](const std::string& v) { path = v; })
        .flag({"--all"}, [&] { show_all = true; })
        .parse(args);
    if (path.empty()) throw std::runtime_error("--program FILE is required");
    const json saved = read_json(path);
    EngineConfig cfg;
    if (saved.contains("config")) config_from_json(cfg, saved.at("config"));
    const json& pj = saved.at("problem");
    auto problem = make_problem(pj.at("name").get<std::string>(), pj.value("params", json::object()), pj.value("seed", cfg.seed));
    const MachineSpec spec = make_machine(*problem, cfg);
    const Program prog = program_from_json(spec, saved.contains("program") ? saved.at("program") : saved);
    std::printf("problem: %s\n  train: %s\n  test:  %s\n\n%s", problem->name().c_str(),
                metrics_line(problem->metrics(spec, prog, Split::Train)).c_str(),
                metrics_line(problem->metrics(spec, prog, Split::Test)).c_str(),
                to_pseudocode(spec, prog, problem->num_outputs(), !show_all).c_str());
    return 0;
}

}  // namespace adaptogen::cli
