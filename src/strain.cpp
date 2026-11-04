#include "adaptogen/strain.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <fstream>
#include <regex>
#include <set>
#include <stdexcept>

#include "adaptogen/analysis.hpp"
#include "adaptogen/registry.hpp"
#include "adaptogen/serialize.hpp"

namespace adaptogen {

using nlohmann::json;
namespace fs = std::filesystem;

namespace {

std::string now_iso() {
    const std::time_t t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
    gmtime_r(&t, &tm);
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

json channel_json(const Channel& c) {
    return json{{"name", c.name}, {"kind", to_string(c.kind)}, {"arity", c.arity}, {"description", c.description},
                {"units", c.units}};
}

Channel channel_from(const json& j) {
    Channel c;
    c.name = j.at("name").get<std::string>();
    const std::string kind = j.value("kind", "sensor");
    if (kind != "sensor" && kind != "action") throw std::runtime_error("channel kind must be sensor or action");
    c.kind = kind == "sensor" ? ChannelKind::Sensor : ChannelKind::Action;
    c.arity = j.value("arity", 0);
    c.description = j.value("description", "");
    c.units = j.value("units", "");
    return c;
}

json metrics_json(const std::vector<Metric>& ms) {
    json j = json::object();
    for (const auto& m : ms) j[m.name] = m.value;
    return j;
}

json read_file(const fs::path& p) {
    std::ifstream in(p);
    if (!in) throw std::runtime_error("cannot open '" + p.string() + "'");
    return json::parse(in);
}

StrainIndexEntry entry_of(const Strain& s) {
    StrainIndexEntry e;
    e.id = s.id;
    e.name = s.name;
    e.version = s.version;
    e.description = s.description;
    e.problem = s.problem.value("name", "");
    if (s.problem.contains("params") && !s.problem.at("params").empty()) e.problem += " " + s.problem.at("params").dump();
    e.created = s.created;
    for (const auto& c : s.channels) e.channels.push_back(c.name);
    e.parents = s.parents;
    e.test_metrics = s.test_metrics;
    e.solved_test = s.solved_test;
    return e;
}

const Channel* find_channel(const std::vector<Channel>& chans, const std::string& name, ChannelKind kind) {
    for (const auto& c : chans)
        if (c.name == name && c.kind == kind) return &c;
    return nullptr;
}

std::string mapped_name(const ChannelMap& map, const std::string& name) {
    for (const auto& [from, to] : map)
        if (from == name) return to;
    return name;
}

}  // namespace

// ------------------------------------------------------------------ JSON

json strain_to_json(const Strain& s) {
    json chans = json::array();
    for (std::size_t i = 0; i < s.channels.size(); ++i) {
        json c = channel_json(s.channels[i]);
        c["used"] = i < s.channel_used.size() && s.channel_used[i];
        chans.push_back(std::move(c));
    }
    json ops = json::array();
    for (Op op : s.ops) ops.push_back(std::string(op_info(op).name));
    return json{
        {"format", "adaptogen-strain/1"},
        {"id", s.id},
        {"name", s.name},
        {"version", s.version},
        {"description", s.description},
        {"created", s.created},
        {"interface", {{"channels", chans}, {"num_outputs", s.num_outputs}, {"registers", s.num_registers}, {"ops", ops}}},
        {"behaviour", s.program},
        {"lineage",
         {{"parents", s.parents},
          {"problem", s.problem},
          {"config", s.config},
          {"generations", s.generations},
          {"program_evaluations", s.program_evaluations}}},
        {"scores", {{"train", s.train_metrics}, {"test", s.test_metrics}, {"solved_test", s.solved_test}}},
    };
}

Strain strain_from_json(const json& j) {
    if (j.value("format", "") != "adaptogen-strain/1") throw std::runtime_error("not an adaptogen strain file");
    Strain s;
    s.id = j.value("id", "");
    s.name = j.value("name", "");
    s.version = j.value("version", 0);
    s.description = j.value("description", "");
    s.created = j.value("created", "");
    const json& in = j.at("interface");
    for (const auto& c : in.at("channels")) {
        s.channels.push_back(channel_from(c));
        s.channel_used.push_back(c.value("used", false));
    }
    s.num_outputs = in.value("num_outputs", 1);
    s.num_registers = in.value("registers", 8);
    for (const auto& o : in.value("ops", json::array())) {
        auto op = op_from_name(o.get<std::string>());
        if (op) s.ops.push_back(*op);
    }
    s.program = j.at("behaviour");
    const json& lin = j.value("lineage", json::object());
    for (const auto& p : lin.value("parents", json::array())) s.parents.push_back(p.get<std::string>());
    s.problem = lin.value("problem", json::object());
    s.config = lin.value("config", json::object());
    s.generations = lin.value("generations", 0);
    s.program_evaluations = lin.value("program_evaluations", 0L);
    const json& sc = j.value("scores", json::object());
    s.train_metrics = sc.value("train", json::object());
    s.test_metrics = sc.value("test", json::object());
    s.solved_test = sc.value("solved_test", false);
    return s;
}

Strain make_strain(const std::string& name, const std::string& description, const Problem& problem,
                   const MachineSpec& spec, const EvolutionResult& result, const json& problem_spec,
                   const EngineConfig& config, std::vector<std::string> parents) {
    Strain s;
    s.name = name;
    s.description = description;
    s.channels = spec.channels;
    s.channel_used.assign(spec.channels.size(), false);
    const auto mask = effective_mask(spec, result.best, problem.num_outputs());
    for (std::size_t i = 0; i < result.best.code.size(); ++i) {
        const Instr& in = result.best.code[i];
        if (mask[i] && (in.op == Op::Sense || in.op == Op::Act) && in.channel < s.channel_used.size())
            s.channel_used[in.channel] = true;
    }
    s.num_outputs = problem.num_outputs();
    s.num_registers = spec.num_registers;
    s.ops = spec.ops;
    s.program = program_to_json(spec, result.best);
    s.parents = std::move(parents);
    s.problem = problem_spec;
    s.config = config_to_json(config);
    s.generations = result.generations;
    s.program_evaluations = result.program_evaluations;
    s.train_metrics = metrics_json(result.train_metrics);
    s.test_metrics = metrics_json(result.test_metrics);
    s.solved_test = result.solved_test;
    return s;
}

// ------------------------------------------------------------------ library

StrainLibrary::StrainLibrary(fs::path dir) : dir_(std::move(dir)) {}

bool StrainLibrary::valid_name(const std::string& name) {
    static const std::regex ok("^[a-z0-9][a-z0-9._-]{0,63}$");
    static const std::regex versioned(".*-v[0-9]+$");
    return std::regex_match(name, ok) && !std::regex_match(name, versioned);
}

std::string StrainLibrary::save(Strain s) {
    if (!valid_name(s.name))
        throw std::runtime_error("invalid strain name '" + s.name +
                                 "': use lowercase letters, digits, '.', '_' or '-', and don't end in -v<number>");
    fs::create_directories(dir_);
    int version = 0;
    for (const auto& e : list())
        if (e.name == s.name) version = std::max(version, e.version);
    s.version = version + 1;
    s.id = s.name + "-v" + std::to_string(s.version);
    if (s.created.empty()) s.created = now_iso();
    const fs::path file = dir_ / (s.id + ".json");
    if (fs::exists(file)) throw std::runtime_error("strain file '" + file.string() + "' already exists");
    {
        std::ofstream out(file);
        if (!out) throw std::runtime_error("cannot write '" + file.string() + "'");
        out << strain_to_json(s).dump(2) << '\n';
    }
    auto entries = read_index();
    entries.push_back(entry_of(s));
    write_index(entries);
    return s.id;
}

Strain StrainLibrary::load(const std::string& id_or_name) const {
    const fs::path direct = dir_ / (id_or_name + ".json");
    if (fs::exists(direct)) return strain_from_json(read_file(direct));
    const StrainIndexEntry* best = nullptr;
    const auto entries = list();
    for (const auto& e : entries)
        if (e.name == id_or_name && (!best || e.version > best->version)) best = &e;
    if (!best) throw std::runtime_error("no strain '" + id_or_name + "' in library '" + dir_.string() + "'");
    return strain_from_json(read_file(dir_ / (best->id + ".json")));
}

std::vector<StrainIndexEntry> StrainLibrary::list() const { return read_index(); }

std::vector<StrainIndexEntry> StrainLibrary::read_index() const {
    const fs::path idx = dir_ / "index.json";
    if (!fs::exists(dir_)) return {};
    if (!fs::exists(idx)) reindex();
    std::vector<StrainIndexEntry> out;
    for (const auto& j : read_file(idx).value("strains", json::array())) {
        StrainIndexEntry e;
        e.id = j.value("id", "");
        e.name = j.value("name", "");
        e.version = j.value("version", 0);
        e.description = j.value("description", "");
        e.problem = j.value("problem", "");
        e.created = j.value("created", "");
        for (const auto& c : j.value("channels", json::array())) e.channels.push_back(c.get<std::string>());
        for (const auto& p : j.value("parents", json::array())) e.parents.push_back(p.get<std::string>());
        e.test_metrics = j.value("test_metrics", json::object());
        e.solved_test = j.value("solved_test", false);
        out.push_back(std::move(e));
    }
    return out;
}

void StrainLibrary::write_index(const std::vector<StrainIndexEntry>& entries) const {
    json arr = json::array();
    for (const auto& e : entries)
        arr.push_back(json{{"id", e.id},
                           {"name", e.name},
                           {"version", e.version},
                           {"description", e.description},
                           {"problem", e.problem},
                           {"created", e.created},
                           {"channels", e.channels},
                           {"parents", e.parents},
                           {"test_metrics", e.test_metrics},
                           {"solved_test", e.solved_test}});
    fs::create_directories(dir_);
    const fs::path tmp = dir_ / "index.json.tmp";
    {
        std::ofstream out(tmp);
        if (!out) throw std::runtime_error("cannot write strain index in '" + dir_.string() + "'");
        out << json{{"format", "adaptogen-strain-index/1"}, {"strains", arr}}.dump(2) << '\n';
    }
    fs::rename(tmp, dir_ / "index.json");
}

void StrainLibrary::reindex() const {
    std::vector<StrainIndexEntry> entries;
    if (fs::exists(dir_))
        for (const auto& f : fs::directory_iterator(dir_)) {
            if (f.path().extension() != ".json" || f.path().filename() == "index.json") continue;
            try {
                entries.push_back(entry_of(strain_from_json(read_file(f.path()))));
            } catch (const std::exception&) {
                // not a strain file: ignore
            }
        }
    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
        return a.name != b.name ? a.name < b.name : a.version < b.version;
    });
    write_index(entries);
}

std::vector<Strain> StrainLibrary::ancestry(const std::string& id_or_name) const {
    std::vector<Strain> out;
    std::set<std::string> seen;
    std::vector<std::string> queue{load(id_or_name).id};
    for (std::size_t i = 0; i < queue.size(); ++i) {
        if (!seen.insert(queue[i]).second) continue;
        try {
            Strain s = load(queue[i]);
            for (const auto& p : s.parents) queue.push_back(p);
            out.push_back(std::move(s));
        } catch (const std::exception&) {
            // parent not in this library
        }
    }
    return out;
}

// ------------------------------------------------------------------ compatibility & revival

double Compatibility::coverage() const {
    const int used_total = used_missing + static_cast<int>(std::count_if(matched.begin(), matched.end(), [](const std::string& m) {
                                              return m.rfind("*", 0) == 0;
                                          }));
    return used_total == 0 ? 1.0 : 1.0 - static_cast<double>(used_missing) / used_total;
}

Compatibility check_compatibility(const Strain& strain, const std::vector<Channel>& problem_channels, const ChannelMap& map) {
    Compatibility c;
    std::vector<bool> targeted(problem_channels.size(), false);
    for (std::size_t i = 0; i < strain.channels.size(); ++i) {
        const Channel& sc = strain.channels[i];
        const bool used = i < strain.channel_used.size() && strain.channel_used[i];
        const std::string target = mapped_name(map, sc.name);
        const Channel* pc = find_channel(problem_channels, target, sc.kind);
        // "*" marks channels the strain's code actually uses (see coverage()).
        const std::string label = (used ? "*" : "") + sc.name + (target != sc.name ? " -> " + target : "");
        if (!pc) {
            c.missing.push_back(label);
            c.used_missing += used;
            continue;
        }
        if (pc->arity != sc.arity) {
            c.missing.push_back(label);
            c.used_missing += used;
            c.warnings.push_back(sc.name + ": arity differs (" + std::to_string(sc.arity) + " vs " +
                                 std::to_string(pc->arity) + "), treated as missing");
            continue;
        }
        targeted[static_cast<std::size_t>(pc - problem_channels.data())] = true;
        c.matched.push_back(label);
        if (pc->units != sc.units)
            c.warnings.push_back(sc.name + ": units differ ('" + sc.units + "' vs '" + pc->units + "'), meaning may differ");
        else if (pc->description != sc.description)
            c.warnings.push_back(sc.name + ": descriptions differ ('" + sc.description + "' vs '" + pc->description + "')");
    }
    for (std::size_t k = 0; k < problem_channels.size(); ++k)
        if (!targeted[k]) c.added.push_back(problem_channels[k].name);
    return c;
}

Program revive(const Strain& strain, const MachineSpec& target, const ChannelMap& map) {
    json prog = strain.program;
    for (auto& in : prog.at("code")) {
        if (!in.contains("channel")) continue;
        const std::string name = in.at("channel").get<std::string>();
        const std::string op = in.value("op", "");
        const ChannelKind kind = op == "act" ? ChannelKind::Action : ChannelKind::Sensor;
        const Channel* sc = find_channel(strain.channels, name, kind);
        const std::string mapped = mapped_name(map, name);
        const Channel* tc = find_channel(target.channels, mapped, kind);
        in["channel"] = (tc && sc && tc->arity == sc->arity) ? mapped : "";  // "" -> pseudogene
    }
    return program_from_json(target, prog);
}

// ------------------------------------------------------------------ seeded runs

PreparedRun prepare_seeding(const Problem& primary, const EngineConfig& cfg) {
    PreparedRun run;
    const auto& sd = cfg.seeding;
    if (sd.strains.empty()) return run;
    StrainLibrary lib(sd.library);
    const MachineSpec spec = make_machine(primary, cfg);
    for (const auto& id : sd.strains) {
        Strain s = lib.load(id);
        run.compatibility.push_back(check_compatibility(s, spec.channels, sd.channel_map));
        run.seeds.push_back(revive(s, spec, sd.channel_map));
        run.strains.push_back(std::move(s));
    }
    if (sd.retain_weight > 0) {
        register_builtin_problems();
        std::vector<const Problem*> parents;
        for (const auto& s : run.strains) {
            if (!s.problem.contains("name")) throw std::runtime_error("strain " + s.id + " does not record its problem");
            run.parent_problems.push_back(make_problem(s.problem.at("name").get<std::string>(),
                                                       s.problem.value("params", json::object()),
                                                       s.problem.value("seed", std::uint64_t{1})));
            parents.push_back(run.parent_problems.back().get());
        }
        run.retention = std::make_unique<RetentionProblem>(primary, parents, sd.retain_weight, cfg);
    }
    return run;
}

EvolutionResult run_evolution(const Problem& problem, const EngineConfig& cfg,
                              const std::function<void(const GenerationReport&)>& on_generation, PreparedRun* prepared_out) {
    PreparedRun prepared = prepare_seeding(problem, cfg);
    Engine engine(prepared.problem(problem), cfg);
    if (!prepared.seeds.empty()) engine.set_seed_programs(prepared.seeds, cfg.seeding.random_fraction);
    if (on_generation) engine.on_generation(on_generation);
    EvolutionResult result = engine.run();
    if (prepared_out) *prepared_out = std::move(prepared);
    return result;
}

// ------------------------------------------------------------------ retention

RetentionProblem::RetentionProblem(const Problem& primary, std::vector<const Problem*> parents, double weight,
                                   const EngineConfig& cfg)
    : primary_(primary), weight_(std::clamp(weight, 0.0, 0.99)) {
    const auto pchans = primary.channels();
    for (const Problem* p : parents) {
        Parent par{p, make_machine(*p, cfg), {}};
        for (const auto& c : pchans) {
            int idx = -1;
            for (std::size_t k = 0; k < par.spec.channels.size(); ++k) {
                const Channel& pc = par.spec.channels[k];
                if (pc.name == c.name && pc.kind == c.kind && pc.arity == c.arity) idx = static_cast<int>(k);
            }
            par.channel_to_parent.push_back(idx);
        }
        parents_.push_back(std::move(par));
    }
}

std::string RetentionProblem::description() const {
    std::string d = primary_.description() + " (retaining";
    for (const auto& p : parents_) d += " " + p.problem->name();
    return d + " with weight " + std::to_string(weight_) + ")";
}

std::size_t RetentionProblem::num_cases(Split s) const {
    std::size_t n = primary_.num_cases(s);
    if (s == Split::Train)
        for (const auto& p : parents_) n += p.problem->num_cases(Split::Train);
    return n;
}

Program RetentionProblem::translate(const Program& p, const Parent& parent) const {
    Program t = p;
    for (Instr& in : t.code) {
        if (in.op != Op::Sense && in.op != Op::Act) continue;
        const int k = in.channel < parent.channel_to_parent.size() ? parent.channel_to_parent[in.channel] : -1;
        if (k < 0) in = Instr{};
        else in.channel = static_cast<std::uint16_t>(k);
    }
    return t;
}

double RetentionProblem::evaluate_case(Runner& runner, const Program& program, Split split, std::size_t index) const {
    const std::size_t n0 = primary_.num_cases(split);
    if (split == Split::Test || parents_.empty()) return primary_.evaluate_case(runner, program, split, index);
    std::size_t parent_total = 0;
    for (const auto& p : parents_) parent_total += p.problem->num_cases(Split::Train);
    const double N = static_cast<double>(n0 + parent_total);
    if (index < n0) return primary_.evaluate_case(runner, program, split, index) * (1.0 - weight_) * N / static_cast<double>(n0);
    std::size_t j = index - n0;
    for (const auto& p : parents_) {
        const std::size_t n = p.problem->num_cases(Split::Train);
        if (j < n) {
            Runner r(p.spec);
            const double s = p.problem->evaluate_case(r, translate(program, p), Split::Train, j);
            runner.add_steps(r.steps());
            return s * weight_ * N / static_cast<double>(parent_total);
        }
        j -= n;
    }
    return 0.0;
}

std::vector<Metric> RetentionProblem::metrics(const MachineSpec& spec, const Program& program, Split split) const {
    auto ms = primary_.metrics(spec, program, split);
    for (const auto& p : parents_) {
        const auto pm = p.problem->metrics(p.spec, translate(program, p), split);
        if (!pm.empty()) ms.push_back({"retained:" + p.problem->name() + ":" + pm[0].name, pm[0].value});
    }
    return ms;
}

}  // namespace adaptogen
