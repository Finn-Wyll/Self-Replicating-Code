// adaptogen MCP server: lets Claude (or any MCP client) define problems, evolve programs, and
// inspect, reuse and export the results. JSON-RPC 2.0 over stdio, one message per line.
#include "adaptogen/mcp.hpp"

#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>

#include "adaptogen/analysis.hpp"
#include "adaptogen/decide.hpp"
#include "adaptogen/plugin.hpp"
#include "adaptogen/registry.hpp"
#include "adaptogen/report.hpp"
#include "adaptogen/serialize.hpp"
#include "adaptogen/simplify.hpp"
#include "adaptogen/strain.hpp"

namespace adaptogen {

using nlohmann::json;
namespace fs = std::filesystem;

namespace {

const char* kInstructions = R"(adaptogen evolves programs (reusable strategies) that solve problems from very little prior knowledge, inspired by how bacteria evolve resistance.

Typical workflow for a user's scenario:
1. list_problems: is there a built-in problem that fits (scheduling, errands, packing, meals, bills, energy, foraging, formula, list)? Check describe_problem for its channels and parameters.
2. If not, write a plug-in: plugin_new creates a template (optionally deriving from saved strains, so their evolved code can be reused). Edit it with plugin_write (C++, adaptogen SDK), then plugin_validate until it reports ok, fixing every error it lists. Give partial credit in scores, keep evaluate_case deterministic, and keep train and test cases separate.
3. evolve_start runs evolution in the background. Poll evolve_status (with wait_seconds) until state is "done". Use problem "plugin:<path.cpp>" for plug-ins.
4. Report the evolved program (readable pseudo-code), its scores on unseen test cases and any comparison metrics. Offer to save it as a strain (save_strain) for later reuse, to export it as C++ (export_cpp), or to show how it evolved (the lineage in the result).
Be honest about limits: evolution is a search. Results vary between seeds, and the test metrics are what count.)";

std::string text_of(const json& j) { return j.is_string() ? j.get<std::string>() : j.dump(2); }

json tool(const std::string& name, const std::string& description, json properties, std::vector<std::string> required = {}) {
    return json{{"name", name},
                {"description", description},
                {"inputSchema", {{"type", "object"}, {"properties", properties}, {"required", required}}}};
}

json metrics_json(const std::vector<Metric>& ms) {
    json j = json::array();
    for (const auto& m : ms) j.push_back({{"name", m.name}, {"value", m.value}});
    return j;
}

json channels_json(const std::vector<Channel>& chans) {
    json j = json::array();
    for (const auto& c : chans)
        j.push_back({{"name", c.name}, {"kind", to_string(c.kind)}, {"arity", c.arity}, {"description", c.description}, {"units", c.units}});
    return j;
}

struct Job {
    std::string id, problem;
    std::atomic<bool> stop{false};
    std::mutex m;
    std::condition_variable cv;
    std::string state = "running";  // running | done | failed | cancelled
    json progress = json::object(), result = json::object();
    std::string error;
    std::thread thread;
};

}  // namespace

struct McpServer::Impl {
    fs::path workspace, library;
    std::mutex jobs_mu;
    std::map<std::string, std::shared_ptr<Job>> jobs;
    int next_job = 1;

    ~Impl() {
        for (auto& [id, job] : jobs) {
            job->stop = true;
            if (job->thread.joinable()) job->thread.join();
        }
    }

    fs::path inside_workspace(const std::string& path) const {
        fs::path p = fs::weakly_canonical(fs::absolute(workspace / path));
        const fs::path root = fs::weakly_canonical(fs::absolute(workspace));
        const auto rel = p.lexically_relative(root);
        if (rel.empty() || rel.native()[0] == '.') throw std::runtime_error("path '" + path + "' is outside the workspace " + root.string());
        return p;
    }

    std::string problem_path(const std::string& name) const {  // plug-in paths are relative to the workspace
        if (name.rfind("plugin:", 0) == 0) return "plugin:" + inside_workspace(name.substr(7)).string();
        return name;
    }

    json tools() const {
        const json obj = {{"type", "object"}};
        const json str = {{"type", "string"}};
        const json num = {{"type", "number"}};
        const json integer = {{"type", "integer"}};
        const json boolean = {{"type", "boolean"}};
        const json strs = {{"type", "array"}, {"items", str}};
        return json::array({
            tool("list_problems", "List the problems adaptogen can evolve programs for, with their parameters.", json::object()),
            tool("describe_problem", "Channels (what programs can observe and do), case counts and outputs of a problem.",
                 {{"problem", str}, {"params", obj}}, {"problem"}),
            tool("evolve_start",
                 "Start evolving a program in the background; returns a job id for evolve_status. Use problem 'plugin:<file.cpp>' "
                 "for plug-ins (paths relative to the workspace). config takes any engine settings (see the README), e.g. "
                 "{\"population\":500, \"generations\":1000, \"length_cost\":0.001}.",
                 {{"problem", str},
                  {"params", obj},
                  {"config", obj},
                  {"seed", integer},
                  {"seed_strains", strs},
                  {"seed_parents", boolean},
                  {"retain_weight", num},
                  {"save_strain", str},
                  {"description", str}},
                 {"problem"}),
            tool("evolve_status",
                 "Progress or final result of an evolution job. wait_seconds (max 120) waits for it to finish first. The final "
                 "result includes readable pseudo-code, train and unseen-test metrics, the lineage of improvements and the saved strain.",
                 {{"job_id", str}, {"wait_seconds", num}}, {"job_id"}),
            tool("evolve_cancel", "Stop a running evolution job (it finishes the current generation and keeps the best so far).",
                 {{"job_id", str}}, {"job_id"}),
            tool("plugin_new",
                 "Create a plug-in template NAME.cpp in the workspace (a working toy problem to replace). With from_strains, it "
                 "derives from saved strains through a generated base class NAME_base.hpp (their channels are inherited). Returns the files.",
                 {{"name", str}, {"from_strains", strs}, {"renames", obj}}, {"name"}),
            tool("plugin_write", "Write a .cpp/.hpp file in the workspace (to create or edit a plug-in).",
                 {{"path", str}, {"content", str}}, {"path", "content"}),
            tool("plugin_read", "Read a .cpp/.hpp file from the workspace.", {{"path", str}}, {"path"}),
            tool("plugin_validate",
                 "Build a plug-in and check it: compile errors (with lines), interface, inheritance rules, crashes, determinism, "
                 "NaN scores, ungraded scores. Fix every error before evolving.",
                 {{"path", str}, {"params", obj}}, {"path"}),
            tool("strains_list", "List the saved strains (evolved programs) in the library.", json::object()),
            tool("strain_show", "A strain's interface, behaviour (pseudo-code), scores and lineage.", {{"id", str}}, {"id"}),
            tool("strain_check", "How well a strain's interface fits a problem (matched / missing / new channels, warnings).",
                 {{"id", str}, {"problem", str}, {"params", obj}}, {"id", "problem"}),
            tool("strain_simplify", "Save a simplified new version of a strain (dead code removed, verified on its training cases).",
                 {{"id", str}}, {"id"}),
            tool("strain_lineage", "Markdown report of a strain's ancestry: problems, scores and how the code changed.", {{"id", str}}, {"id"}),
            tool("strain_decide",
                 "Use a strain as a fast local decision function: give the options (objects of feature -> number) and optional "
                 "context; returns the chosen index, every option's score, the margin and (for decision strains) a calibrated "
                 "confidence. Low confidence = consider deferring to a person or an LLM.",
                 {{"id", str}, {"options", {{"type", "array"}, {"items", obj}}}, {"context", obj}}, {"id", "options"}),
            tool("strain_adapt",
                 "Adapt a data-driven strain (e.g. from the 'decisions' problem) to new outcomes: evolve again on old + new data "
                 "starting from the strain, in the background (poll evolve_status). only_new=true specialises it to the new "
                 "data alone (e.g. one business's own logs). Saved as the next version only if it is not worse on held-out cases.",
                 {{"id", str}, {"data", strs}, {"only_new", boolean}, {"generations", integer}}, {"id", "data"}),
            tool("export_code",
                 "Export a strain as plug-in code for an app (language: cpp, python or javascript): exact run(), choose(options) "
                 "and a header with its held-out results compared with simple solutions.",
                 {{"id", str}, {"language", str}, {"name", str}}, {"id"}),
            tool("export_cpp", "Export a strain as a self-contained C++ header (same as export_code with language cpp).",
                 {{"id", str}, {"name", str}}, {"id"}),
        });
    }

    json call(const std::string& name, const json& a) {
        StrainLibrary lib(library);
        if (name == "list_problems") {
            json out = json::array();
            for (const auto& e : registered_problems()) out.push_back({{"name", e.name}, {"description", e.description}, {"params", e.params_help}});
            return out;
        }
        if (name == "describe_problem") {
            auto p = make_problem(problem_path(a.at("problem").get<std::string>()), a.value("params", json::object()), 1);
            return json{{"name", p->name()},
                        {"description", p->description()},
                        {"channels", channels_json(p->channels())},
                        {"num_outputs", p->num_outputs()},
                        {"train_cases", p->num_cases(Split::Train)},
                        {"test_cases", p->num_cases(Split::Test)}};
        }
        if (name == "evolve_start") return start_job(a);
        if (name == "evolve_status") {
            auto job = find_job(a.at("job_id").get<std::string>());
            const double wait = std::min(120.0, a.value("wait_seconds", 0.0));
            std::unique_lock<std::mutex> lock(job->m);
            if (wait > 0) job->cv.wait_for(lock, std::chrono::duration<double>(wait), [&] { return job->state != "running"; });
            json out = {{"job_id", job->id}, {"problem", job->problem}, {"state", job->state}, {"progress", job->progress}};
            if (job->state != "running") out["result"] = job->result;
            if (!job->error.empty()) out["error"] = job->error;
            return out;
        }
        if (name == "evolve_cancel") {
            auto job = find_job(a.at("job_id").get<std::string>());
            job->stop = true;
            return json{{"job_id", job->id}, {"state", "cancelling"}};
        }
        if (name == "plugin_new") {
            const std::string n = a.at("name").get<std::string>();
            std::string cls;
            bool up = true;
            for (char c : n) {
                if (!std::isalnum(static_cast<unsigned char>(c))) { up = true; continue; }
                cls += up ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : c;
                up = false;
            }
            if (cls.empty() || std::isdigit(static_cast<unsigned char>(cls[0]))) cls = "P" + cls;
            json files = json::object();
            std::string header, base;
            if (a.contains("from_strains") && !a.at("from_strains").empty()) {
                std::vector<Strain> parents;
                for (const auto& id : a.at("from_strains")) parents.push_back(lib.load(id.get<std::string>()));
                std::vector<std::pair<std::string, std::string>> renames;
                const json rn = a.value("renames", json::object());  // must outlive the loop
                for (const auto& [k, v] : rn.items()) renames.emplace_back(k, v.get<std::string>());
                base = cls + "Base";
                header = n + "_base.hpp";
                files[header] = generate_base_header(parents, base, renames);
            }
            files[n + ".cpp"] = plugin_template(n, cls, header, base);
            for (const auto& [f, content] : files.items()) {
                const fs::path p = inside_workspace(f);
                if (fs::exists(p)) throw std::runtime_error("'" + p.string() + "' already exists");
                std::ofstream(p) << content.get<std::string>();
            }
            return json{{"files", files}, {"next", "edit " + n + ".cpp with plugin_write, then plugin_validate it"}};
        }
        if (name == "plugin_write" || name == "plugin_read") {
            const fs::path p = inside_workspace(a.at("path").get<std::string>());
            const auto ext = p.extension().string();
            if (ext != ".cpp" && ext != ".hpp" && ext != ".h" && ext != ".cc")
                throw std::runtime_error("only .cpp/.cc/.hpp/.h files can be written or read");
            if (name == "plugin_read") {
                std::ifstream in(p);
                if (!in) throw std::runtime_error("cannot read '" + p.string() + "'");
                std::ostringstream ss;
                ss << in.rdbuf();
                return ss.str();
            }
            fs::create_directories(p.parent_path());
            std::ofstream(p) << a.at("content").get<std::string>();
            return json{{"written", p.string()}};
        }
        if (name == "plugin_validate") {
            const ValidationReport r = validate_plugin(inside_workspace(a.at("path").get<std::string>()), a.value("params", json::object()), 1, library);
            json diags = json::array();
            for (const auto& d : r.build.diagnostics)
                diags.push_back({{"file", d.file}, {"line", d.line}, {"column", d.column}, {"severity", d.severity}, {"message", d.message}});
            return json{{"ok", r.ok}, {"errors", r.errors}, {"warnings", r.warnings}, {"notes", r.notes}, {"diagnostics", diags}};
        }
        if (name == "strains_list") {
            json out = json::array();
            for (const auto& e : lib.list())
                out.push_back({{"id", e.id}, {"description", e.description}, {"problem", e.problem}, {"channels", e.channels},
                               {"parents", e.parents}, {"test", e.test_metrics}, {"solved_test", e.solved_test}});
            return out;
        }
        const std::string id = a.value("id", "");
        if (name == "strain_show") {
            const Strain s = lib.load(id);
            const MachineSpec spec = strain_machine(s);
            const Program p = program_from_json(spec, s.program);
            return json{{"id", s.id}, {"description", s.description}, {"channels", channels_json(s.channels)},
                        {"pseudocode", to_pseudocode(spec, p, s.num_outputs, true)}, {"problem", s.problem},
                        {"parents", s.parents}, {"test", s.test_metrics}, {"solved_test", s.solved_test}, {"events", s.events}};
        }
        if (name == "strain_check") {
            const Strain s = lib.load(id);
            auto p = make_problem(problem_path(a.at("problem").get<std::string>()), a.value("params", json::object()), 1);
            const Compatibility c = check_compatibility(s, p->channels());
            return json{{"coverage", c.coverage()}, {"matched", c.matched}, {"missing", c.missing}, {"new", c.added}, {"warnings", c.warnings}};
        }
        if (name == "strain_simplify") {
            const Strain s = lib.load(id);
            register_builtin_problems();
            auto problem = make_problem(s.problem.at("name").get<std::string>(), s.problem.value("params", json::object()),
                                        s.problem.value("seed", std::uint64_t{1}));
            EngineConfig cfg;
            if (!s.config.empty()) config_from_json(cfg, s.config);
            const MachineSpec spec = make_machine(*problem, cfg);
            const auto sr = simplify(*problem, spec, program_from_json(spec, s.program));
            EvolutionResult r;
            r.best = sr.program;
            r.generations = s.generations;
            r.program_evaluations = s.program_evaluations;
            r.train_metrics = problem->metrics(spec, r.best, Split::Train);
            r.test_metrics = problem->metrics(spec, r.best, Split::Test);
            r.solved_test = problem->solved(spec, r.best, Split::Test);
            const std::string nid = lib.save(make_strain(s.name, s.description + " (simplified)", *problem, spec, r, s.problem, cfg, {s.id}));
            return json{{"id", nid}, {"length_before", sr.length_before}, {"length_after", sr.length_after},
                        {"pseudocode", to_pseudocode(spec, r.best, problem->num_outputs(), true)}};
        }
        if (name == "strain_lineage") return strain_lineage_report(lib, id);
        if (name == "export_cpp" || name == "export_code") {
            const Strain s = lib.load(id);
            const MachineSpec spec = strain_machine(s);
            return export_code(export_language(a.value("language", std::string("cpp"))), spec, program_from_json(spec, s.program),
                               s.num_outputs, a.value("name", s.id), report_card(s), s.extras);
        }
        if (name == "strain_decide") {
            const Strain s = lib.load(id);
            json out = to_json(decide(s, a.at("options"), a.value("context", json::object())));
            out["strain"] = s.id;
            return out;
        }
        if (name == "strain_adapt") {
            const Strain s = lib.load(id);
            json params = s.problem.value("params", json::object());
            if (!params.contains("data")) throw std::runtime_error("strain_adapt needs a strain evolved from data (e.g. the decisions problem)");
            json files = a.value("only_new", false) ? json::array()
                                                    : (params.at("data").is_array() ? params.at("data") : json::array({params.at("data")}));
            for (const auto& d : a.at("data")) files.push_back(inside_workspace(d.get<std::string>()).string());
            params["data"] = s.problem.value("name", "") == "decisions" ? files : json(files.back());
            json cfg = s.config;
            cfg.erase("seeding");
            if (a.contains("generations")) cfg["generations"] = a.at("generations");
            return start_job({{"problem", s.problem.at("name")}, {"params", params}, {"config", cfg}, {"seed", s.problem.value("seed", 1)},
                              {"seed_strains", json::array({s.id})}, {"save_strain", s.name}, {"description", s.description},
                              {"only_if_not_worse_than", s.id}});
        }
        throw std::runtime_error("unknown tool '" + name + "'");
    }

    std::shared_ptr<Job> find_job(const std::string& id) {
        std::lock_guard<std::mutex> lock(jobs_mu);
        auto it = jobs.find(id);
        if (it == jobs.end()) throw std::runtime_error("no job '" + id + "'");
        return it->second;
    }

    json start_job(const json& a) {
        auto job = std::make_shared<Job>();
        job->problem = a.at("problem").get<std::string>();
        {
            std::lock_guard<std::mutex> lock(jobs_mu);
            job->id = "job-" + std::to_string(next_job++);
            jobs[job->id] = job;
        }
        // Validate the request up front so mistakes come back immediately.
        EngineConfig cfg;
        cfg.seeding.library = library.string();
        if (a.contains("config")) config_from_json(cfg, a.at("config"));
        if (a.contains("seed")) cfg.seed = a.at("seed").get<std::uint64_t>();
        if (a.contains("seed_strains")) cfg.seeding.strains = a.at("seed_strains").get<std::vector<std::string>>();
        if (a.contains("retain_weight")) cfg.seeding.retain_weight = a.at("retain_weight").get<double>();
        const std::string pname = problem_path(job->problem);
        const json params = a.value("params", json::object());
        auto problem = std::shared_ptr<Problem>(make_problem(pname, params, cfg.seed));
        if (a.value("seed_parents", false)) {
            auto* plugin = dynamic_cast<PluginProblem*>(problem.get());
            if (!plugin || plugin->parents().empty()) throw std::runtime_error("seed_parents needs a plug-in problem derived from strains");
            for (const auto& p : plugin->parents()) cfg.seeding.strains.push_back(p);
            for (const auto& m : plugin->channel_maps()) cfg.seeding.channel_map.push_back(m);
        }
        const std::string save = a.value("save_strain", ""), description = a.value("description", "");
        const std::string guard = a.value("only_if_not_worse_than", "");
        if (!save.empty() && !StrainLibrary::valid_name(save)) throw std::runtime_error("invalid strain name '" + save + "'");
        const fs::path lib_dir = library;
        job->thread = std::thread([job, problem, cfg, pname, params, save, description, lib_dir, guard] {
            try {
                PreparedRun prepared;
                const MachineSpec spec = make_machine(*problem, cfg);
                EvolutionResult res = run_evolution(*problem, cfg, [&](const GenerationReport& r) {
                    std::lock_guard<std::mutex> lock(job->m);
                    job->progress = {{"generation", r.generation}, {"seconds", r.seconds}, {"case_evaluations", r.case_evaluations},
                                     {"best_length", r.best_effective_length}, {"best_train_metrics", metrics_json(r.best_train_metrics)}};
                }, &prepared, &job->stop);
                const Problem& scored = prepared.problem(*problem);
                res.best = simplify(scored, spec, res.best).program;
                res.train_metrics = scored.metrics(spec, res.best, Split::Train);
                res.test_metrics = scored.metrics(spec, res.best, Split::Test);
                res.solved_train = problem->solved(spec, res.best, Split::Train);
                res.solved_test = problem->solved(spec, res.best, Split::Test);
                json lineage = json::array();
                for (const auto& e : res.lineage) lineage.push_back({{"generation", e.generation}, {"fitness", e.fitness}, {"origin", e.origin}});
                json result = {{"generations", res.generations},
                               {"seconds", res.seconds},
                               {"solved_test", res.solved_test},
                               {"train_metrics", metrics_json(res.train_metrics)},
                               {"test_metrics", metrics_json(res.test_metrics)},
                               {"pseudocode", to_pseudocode(spec, res.best, problem->num_outputs(), true)},
                               {"program", program_to_json(spec, res.best)},
                               {"lineage", lineage}};
                bool save_it = !save.empty();
                if (!guard.empty()) {  // adapt: keep the old strain unless the new one is at least as good
                    const Strain old = StrainLibrary(lib_dir).load(guard);
                    const double before = problem->mean_score(spec, revive(old, spec), Split::Test);
                    const double after = problem->mean_score(spec, res.best, Split::Test);
                    result["held_out_score_before"] = before;
                    result["held_out_score_after"] = after;
                    save_it = save_it && after + 1e-12 >= before;
                    if (!save_it) result["not_saved"] = "the adapted program is worse on held-out cases; " + guard + " is kept";
                }
                if (save_it) {
                    std::vector<std::string> parents;
                    for (const auto& s : prepared.strains) parents.push_back(s.id);
                    result["strain"] = StrainLibrary(lib_dir).save(make_strain(save, description, *problem, spec, res,
                                                                               {{"name", pname}, {"params", params}, {"seed", cfg.seed}}, cfg, parents));
                }
                std::lock_guard<std::mutex> lock(job->m);
                job->result = result;
                job->state = job->stop ? "cancelled" : "done";
            } catch (const std::exception& e) {
                std::lock_guard<std::mutex> lock(job->m);
                job->state = "failed";
                job->error = e.what();
            }
            job->cv.notify_all();
        });
        return json{{"job_id", job->id}, {"state", "running"}, {"hint", "poll evolve_status with wait_seconds"}};
    }
};

McpServer::McpServer(fs::path workspace, fs::path library) : impl_(std::make_unique<Impl>()) {
    impl_->workspace = std::move(workspace);
    impl_->library = std::move(library);
    register_builtin_problems();
}

McpServer::~McpServer() = default;

json McpServer::handle(const json& msg) {
    const bool has_id = msg.contains("id");
    const std::string method = msg.value("method", "");
    auto reply = [&](json result) { return json{{"jsonrpc", "2.0"}, {"id", msg.at("id")}, {"result", std::move(result)}}; };
    auto error = [&](int code, const std::string& m) {
        return json{{"jsonrpc", "2.0"}, {"id", has_id ? msg.at("id") : json(nullptr)}, {"error", {{"code", code}, {"message", m}}}};
    };
    if (!has_id) return nullptr;  // notifications (e.g. notifications/initialized) need no answer
    try {
        if (method == "initialize") {
            const std::string version = msg.value("params", json::object()).value("protocolVersion", "2025-06-18");
            return reply({{"protocolVersion", version},
                          {"capabilities", {{"tools", {{"listChanged", false}}}}},
                          {"serverInfo", {{"name", "adaptogen"}, {"version", "0.4.0"}}},
                          {"instructions", kInstructions}});
        }
        if (method == "ping") return reply(json::object());
        if (method == "tools/list") return reply({{"tools", impl_->tools()}});
        if (method == "tools/call") {
            const json& p = msg.at("params");
            try {
                const json out = impl_->call(p.at("name").get<std::string>(), p.value("arguments", json::object()));
                return reply({{"content", json::array({{{"type", "text"}, {"text", text_of(out)}}})}, {"isError", false}});
            } catch (const std::exception& e) {  // tool errors go back to the model so it can fix them
                return reply({{"content", json::array({{{"type", "text"}, {"text", std::string("error: ") + e.what()}}})}, {"isError", true}});
            }
        }
        return error(-32601, "method not found: " + method);
    } catch (const std::exception& e) {
        return error(-32602, e.what());
    }
}

int McpServer::serve(std::istream& in, std::ostream& out) {
    for (std::string line; std::getline(in, line);) {
        if (line.find_first_not_of(" \t\r") == std::string::npos) continue;
        json msg;
        try {
            msg = json::parse(line);
        } catch (const std::exception& e) {
            out << json{{"jsonrpc", "2.0"}, {"id", nullptr}, {"error", {{"code", -32700}, {"message", std::string("parse error: ") + e.what()}}}}.dump()
                << '\n' << std::flush;
            continue;
        }
        const json r = handle(msg);
        if (!r.is_null()) out << r.dump() << '\n' << std::flush;
    }
    return 0;
}

}  // namespace adaptogen
