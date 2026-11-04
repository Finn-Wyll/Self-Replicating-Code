// adaptogen strains ... | decide | adapt | export
#include <cstdio>
#include <iostream>

#include "adaptogen/analysis.hpp"
#include "adaptogen/decide.hpp"
#include "adaptogen/registry.hpp"
#include "adaptogen/report.hpp"
#include "adaptogen/serialize.hpp"
#include "common.hpp"

namespace adaptogen::cli {

namespace {

void strains_list(const StrainLibrary& lib) {
    const auto entries = lib.list();
    if (entries.empty()) std::printf("no strains in %s/ (save one with evolve --save-strain NAME)\n", lib.dir().c_str());
    for (const auto& e : entries) {
        std::string chans, parents, metrics;
        for (const auto& c : e.channels) chans += (chans.empty() ? "" : ", ") + c;
        for (const auto& p : e.parents) parents += (parents.empty() ? "" : ", ") + p;
        for (const auto& [k, v] : e.test_metrics.items()) {
            char buf[64];
            std::snprintf(buf, sizeof buf, "%s%s=%.4g", metrics.empty() ? "" : " ", k.c_str(), v.get<double>());
            metrics += buf;
        }
        std::printf("%-24s %s%s\n    problem: %s\n    channels: %s\n    test: %s%s\n", e.id.c_str(), e.description.c_str(),
                    e.solved_test ? "  [solved]" : "", e.problem.c_str(), chans.c_str(), metrics.c_str(),
                    parents.empty() ? "" : ("\n    parents: " + parents).c_str());
    }
}

void strains_show(const StrainLibrary& lib, const Strain& s, bool show_all) {
    std::printf("strain %s  (created %s)\n  %s\n\ninterface:\n", s.id.c_str(), s.created.c_str(), s.description.c_str());
    for (std::size_t k = 0; k < s.channels.size(); ++k) {
        const Channel& c = s.channels[k];
        std::printf("  %-7s %-22s arity %d%s  %s%s\n", to_string(c.kind), c.name.c_str(), c.arity,
                    c.units.empty() ? "" : (" [" + c.units + "]").c_str(), c.description.c_str(),
                    k < s.channel_used.size() && s.channel_used[k] ? "   (used)" : "");
    }
    std::printf("  outputs: %d register(s), %d registers in total\n", s.num_outputs, s.num_registers);
    const MachineSpec spec = strain_machine(s);
    const Program prog = program_from_json(spec, s.program);
    std::printf("\nbehaviour (%zu instructions, %zu effective):\n%s", prog.size(), effective_length(spec, prog, s.num_outputs),
                to_pseudocode(spec, prog, s.num_outputs, !show_all).c_str());
    std::printf("\nlineage:\n  problem: %s\n  %d generations, %ld program evaluations\n  test: %s%s\n", s.problem.dump().c_str(),
                s.generations, s.program_evaluations, s.test_metrics.dump().c_str(), s.solved_test ? "  [solved]" : "");
    const auto anc = lib.ancestry(s.id);
    if (anc.size() > 1) {
        std::printf("  ancestry:\n");
        for (std::size_t k = 1; k < anc.size(); ++k)
            std::printf("    %s  (%s)\n", anc[k].id.c_str(), anc[k].problem.value("name", "?").c_str());
    }
}

}  // namespace

int cmd_strains(const Args& all) {
    if (all.empty()) throw std::runtime_error("usage: adaptogen strains list|show|check|simplify|lineage|reindex ...");
    const std::string sub = all[0];
    const Args args(all.begin() + 1, all.end());
    std::string library = default_library(), id, problem_name, out_file;
    json params = json::object();
    ChannelMap map;
    bool show_all = false;
    Options()
        .value({"--library"}, [&](const std::string& v) { library = v; })
        .value({"--problem"}, [&](const std::string& v) { problem_name = v; })
        .value({"--params"}, [&](const std::string& v) { params.update(json::parse(v)); })
        .value({"--param"}, [&](const std::string& v) { auto [k, x] = key_value(v); params[k] = x; })
        .value({"--map"}, [&](const std::string& v) { map.push_back(mapping(v, "--map")); })
        .value({"-o"}, [&](const std::string& v) { out_file = v; })
        .flag({"--all"}, [&] { show_all = true; })
        .positional([&](const std::string& v) {
            if (!id.empty()) throw std::runtime_error("unknown option '" + v + "'");
            id = v;
        })
        .parse(args);
    StrainLibrary lib(library);

    if (sub == "reindex") {
        lib.reindex();
        std::printf("indexed %zu strains in %s/\n", lib.list().size(), library.c_str());
        return 0;
    }
    if (sub == "list") {
        strains_list(lib);
        return 0;
    }
    if (id.empty()) throw std::runtime_error("strains " + sub + " needs a strain id or name");
    const Strain s = lib.load(id);
    if (sub == "show") {
        strains_show(lib, s, show_all);
        return 0;
    }
    if (sub == "simplify") {
        const SimplifiedStrain out = simplified_strain(s);
        const std::string id2 = lib.save(out.strain);
        std::printf("%s: %zu -> %zu instructions (%d edits, verified on %zu training cases); saved as %s\n", s.id.c_str(),
                    out.length_before, out.length_after, out.edits, out.training_cases, id2.c_str());
        return 0;
    }
    if (sub == "lineage") {
        write_text(out_file, strain_lineage_report(lib, s.id));
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

int cmd_decide(const Args& args) {
    std::string id, library = default_library();
    json options, context = json::object();
    Options()
        .value({"--options"}, [&](const std::string& v) { options = json_arg(v); })
        .value({"--context"}, [&](const std::string& v) { context = json_arg(v); })
        .value({"--library"}, [&](const std::string& v) { library = v; })
        .positional([&](const std::string& v) {
            if (!id.empty()) throw std::runtime_error("unknown option '" + v + "'");
            id = v;
        })
        .parse(args);
    if (id.empty()) throw std::runtime_error("decide needs a strain id");
    const Strain s = StrainLibrary(library).load(id);
    json out = to_json(decide(s, options, context));
    out["strain"] = s.id;
    std::cout << out.dump(2) << '\n';
    return 0;
}

int cmd_adapt(const Args& args) {
    std::string id, library = default_library();
    std::vector<std::string> data;
    int generations = 0;
    long long seed = -1;
    bool force = false, only_new = false;
    Options()
        .value({"--data"}, [&](const std::string& v) { data.push_back(v); })
        .value({"--generations"}, [&](const std::string& v) { generations = std::stoi(v); })
        .value({"--seed"}, [&](const std::string& v) { seed = std::stoll(v); })
        .value({"--library"}, [&](const std::string& v) { library = v; })
        .flag({"--only-new"}, [&] { only_new = true; })
        .flag({"--force"}, [&] { force = true; })
        .positional([&](const std::string& v) {
            if (!id.empty()) throw std::runtime_error("unknown option '" + v + "'");
            id = v;
        })
        .parse(args);
    if (id.empty() || data.empty()) throw std::runtime_error("usage: adaptogen adapt STRAIN --data NEW.csv");
    StrainLibrary lib(library);
    const Strain s = lib.load(id);
    const json params = adapted_params(s, data, only_new);
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
    finish_result(*problem, *problem, spec, res);
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

int cmd_export(const Args& args) {
    std::string program_file, strain_id, name, out_file, library = default_library(), language = "cpp";
    Options()
        .value({"--program"}, [&](const std::string& v) { program_file = v; })
        .value({"--strain"}, [&](const std::string& v) { strain_id = v; })
        .value({"--name"}, [&](const std::string& v) { name = v; })
        .value({"--language"}, [&](const std::string& v) { language = v; })
        .value({"-o"}, [&](const std::string& v) { out_file = v; })
        .value({"--library"}, [&](const std::string& v) { library = v; })
        .parse(args);
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
    write_text(out_file, code);
    return 0;
}

}  // namespace adaptogen::cli
