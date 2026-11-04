#include "adaptogen/plugin.hpp"

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <thread>

#include "adaptogen/text.hpp"
#include "adaptogen/build_config.hpp"
#include "adaptogen/engine.hpp"
#include "adaptogen/parallel.hpp"
#include "adaptogen/random.hpp"
#include "adaptogen/variation.hpp"

extern char** environ;

namespace adaptogen {

using nlohmann::json;
namespace fs = std::filesystem;

namespace {

struct WorkerFailure : std::runtime_error {
    using std::runtime_error::runtime_error;
};

std::string read_text(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) throw std::runtime_error("cannot read '" + p.string() + "'");
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::uint64_t fnv1a(const std::string& s, std::uint64_t h = 1469598103934665603ULL) {
    for (unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ULL;
    }
    return h;
}

std::string shell_quote(const std::string& s) {
    std::string out = "'";
    for (char c : s) out += c == '\'' ? std::string("'\\''") : std::string(1, c);
    return out + "'";
}

fs::path self_executable_dir() {
#ifdef __APPLE__
    char buf[4096];
    uint32_t size = sizeof buf;
    if (_NSGetExecutablePath(buf, &size) == 0) return fs::weakly_canonical(buf).parent_path();
#else
    std::error_code ec;
    auto p = fs::read_symlink("/proc/self/exe", ec);
    if (!ec) return p.parent_path();
#endif
    return {};
}

fs::path worker_executable() {
    if (const char* env = std::getenv("ADAPTOGEN_WORKER"); env && *env) return env;
    const fs::path next_to_us = self_executable_dir() / "adaptogen-worker";
    if (fs::exists(next_to_us)) return next_to_us;
    return ADAPTOGEN_WORKER_PATH;
}

json encode(const Program& p) {
    json code = json::array();
    for (const Instr& in : p.code)
        code.push_back({static_cast<int>(in.op), in.dst, in.a, in.b, in.c, in.channel, in.imm});
    return code;
}

json machine_message(const MachineSpec& spec) {
    json ops = json::array();
    for (Op op : spec.ops) ops.push_back(static_cast<int>(op));
    return json{{"op", "machine"},
                {"registers", spec.num_registers},
                {"ops", ops},
                {"limits",
                 {{"max_steps", spec.limits.max_steps},
                  {"max_loop_iterations", spec.limits.max_loop_iterations},
                  {"max_loop_body", spec.limits.max_loop_body}}},
                {"const_min", spec.const_min},
                {"const_max", spec.const_max}};
}

}  // namespace

// ------------------------------------------------------------------ worker processes

struct PluginProblem::Worker {
    pid_t pid = -1;
    int to = -1, from = -1;
    std::string buffer;
    std::string machine;  // last machine message sent
    bool alive = false;
    std::mutex m;

    void spawn() {
        static const bool sigpipe_ignored = [] {
            ::signal(SIGPIPE, SIG_IGN);  // a dead worker must not kill the engine
            return true;
        }();
        (void)sigpipe_ignored;
        int in_pipe[2], out_pipe[2];
        if (pipe(in_pipe) != 0 || pipe(out_pipe) != 0) throw std::runtime_error("cannot create pipes for the worker");
        posix_spawn_file_actions_t fa;
        posix_spawn_file_actions_init(&fa);
        posix_spawn_file_actions_adddup2(&fa, in_pipe[0], 0);
        posix_spawn_file_actions_adddup2(&fa, out_pipe[1], 1);
        posix_spawn_file_actions_addclose(&fa, in_pipe[1]);
        posix_spawn_file_actions_addclose(&fa, out_pipe[0]);
        const std::string exe = worker_executable().string();
        char* argv[] = {const_cast<char*>(exe.c_str()), nullptr};
        const int rc = posix_spawn(&pid, exe.c_str(), &fa, nullptr, argv, environ);
        posix_spawn_file_actions_destroy(&fa);
        ::close(in_pipe[0]);
        ::close(out_pipe[1]);
        if (rc != 0) {
            ::close(in_pipe[1]);
            ::close(out_pipe[0]);
            throw std::runtime_error("cannot start the plug-in worker '" + exe + "': " + std::strerror(rc) +
                                     " (set ADAPTOGEN_WORKER to its path)");
        }
        to = in_pipe[1];
        from = out_pipe[0];
        ::fcntl(to, F_SETFD, FD_CLOEXEC);
        ::fcntl(from, F_SETFD, FD_CLOEXEC);
        buffer.clear();
        machine.clear();
        alive = true;
    }

    void kill() {
        if (to >= 0) ::close(to);
        if (from >= 0) ::close(from);
        to = from = -1;
        if (pid > 0) {
            ::kill(pid, SIGKILL);
            int status;
            ::waitpid(pid, &status, 0);
        }
        pid = -1;
        alive = false;
    }

    ~Worker() {
        if (alive && to >= 0) {
            const char quit[] = "{\"op\":\"quit\"}\n";
            (void)!::write(to, quit, sizeof quit - 1);
        }
        kill();
    }

    void send(const std::string& line) {
        std::string data = line + "\n";
        const char* p = data.data();
        std::size_t left = data.size();
        while (left > 0) {
            const ssize_t n = ::write(to, p, left);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) throw WorkerFailure("the plug-in worker died (write failed)");
            p += n;
            left -= static_cast<std::size_t>(n);
        }
    }

    std::string receive(double timeout_s) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::duration<double>(timeout_s);
        for (;;) {
            if (auto nl = buffer.find('\n'); nl != std::string::npos) {
                std::string line = buffer.substr(0, nl);
                buffer.erase(0, nl + 1);
                return line;
            }
            const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
            if (left <= 0) throw WorkerFailure("the plug-in did not answer within " + std::to_string(timeout_s) + " s");
            pollfd pfd{from, POLLIN, 0};
            const int r = ::poll(&pfd, 1, static_cast<int>(std::min<long long>(left, 1000000)));
            if (r < 0 && errno == EINTR) continue;
            if (r == 0) continue;
            char buf[65536];
            const ssize_t n = ::read(from, buf, sizeof buf);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) throw WorkerFailure("the plug-in worker died (crash or exit)");
            buffer.append(buf, static_cast<std::size_t>(n));
        }
    }
};

PluginProblem::PluginProblem(fs::path library, json params, std::uint64_t seed)
    : library_(fs::absolute(std::move(library))), params_(std::move(params)), seed_(seed) {
    auto w = acquire(0);
    std::lock_guard<std::mutex> lock(w->m);
    // acquire() already initialised the worker and read the info.
}

PluginProblem::~PluginProblem() = default;

std::shared_ptr<PluginProblem::Worker> PluginProblem::acquire(std::size_t slot) const {
    std::lock_guard<std::mutex> lock(mu_);
    while (workers_.size() <= slot) workers_.push_back(nullptr);
    auto& w = workers_[slot];
    if (w && w->alive) return w;
    if (!w) w = std::make_shared<Worker>();
    w->kill();
    w->spawn();
    w->send(json{{"op", "init"}, {"plugin", library_.string()}, {"params", params_}, {"seed", seed_}}.dump());
    json reply;
    try {
        reply = json::parse(w->receive(timeout_seconds));
    } catch (const WorkerFailure& e) {
        w->kill();
        throw std::runtime_error(std::string("the plug-in crashed while starting: ") + e.what());
    }
    if (!reply.value("ok", false)) {
        w->kill();
        throw std::runtime_error(reply.value("error", "plug-in initialisation failed"));
    }
    if (info_.is_null()) {
        auto* self = const_cast<PluginProblem*>(this);
        self->info_ = reply.at("info");
        for (const auto& c : info_.at("channels"))
            self->channels_.push_back({c.at("name").get<std::string>(),
                                       c.value("kind", "sensor") == "action" ? ChannelKind::Action : ChannelKind::Sensor,
                                       c.value("arity", 0), c.value("description", ""), c.value("units", "")});
    }
    return w;
}

json PluginProblem::request(Worker& w, const json& req, const MachineSpec* spec) const {
    try {
        if (spec) {
            const std::string m = machine_message(*spec).dump();
            if (w.machine != m) {
                w.send(m);
                const json r = json::parse(w.receive(timeout_seconds));
                if (!r.value("ok", false)) throw std::runtime_error(r.value("error", "machine setup failed"));
                w.machine = m;
            }
        }
        w.send(req.dump());
        json r = json::parse(w.receive(timeout_seconds));
        if (!r.value("ok", false)) throw std::runtime_error("plug-in error: " + r.value("error", "unknown"));
        return r;
    } catch (const WorkerFailure&) {
        w.kill();
        throw;
    } catch (const json::exception& e) {
        w.kill();
        throw WorkerFailure(std::string("garbled reply from the plug-in worker: ") + e.what());
    }
}

std::vector<Op> PluginProblem::default_ops() const {
    std::vector<Op> ops;
    for (const auto& o : info_.value("default_ops", json::array()))
        if (auto op = op_from_name(o.get<std::string>()); op && *op != Op::Sense && *op != Op::Act && *op != Op::Nop)
            ops.push_back(*op);
    return ops.empty() ? core_ops() : ops;
}

std::size_t PluginProblem::num_cases(Split s) const {
    return info_.at("cases").value(s == Split::Train ? "train" : "test", std::size_t{0});
}

std::vector<std::string> PluginProblem::parents() const {
    std::vector<std::string> out;
    for (const auto& p : info_.value("parents", json::array())) out.push_back(p.get<std::string>());
    return out;
}

std::vector<std::pair<std::string, std::string>> PluginProblem::channel_maps() const {
    std::vector<std::pair<std::string, std::string>> out;
    const json maps = info_.value("channel_maps", json::object());  // must outlive the loop
    for (const auto& [k, v] : maps.items()) out.emplace_back(k, v.get<std::string>());
    return out;
}

void PluginProblem::score_chunk(std::size_t slot, const MachineSpec& spec, const std::vector<const Program*>& programs,
                                std::size_t begin, std::size_t end, Split split, const std::vector<std::size_t>& cases,
                                std::vector<double>& scores, std::vector<long>& steps) const {
    const std::size_t n = cases.size();
    auto run_range = [&](std::size_t b, std::size_t e) {
        auto w = acquire(slot);
        std::lock_guard<std::mutex> lock(w->m);
        json progs = json::array();
        for (std::size_t p = b; p < e; ++p) progs.push_back(encode(*programs[p]));
        const json r = request(*w, json{{"op", "score"}, {"split", split == Split::Train ? 0 : 1}, {"cases", cases}, {"programs", progs}}, &spec);
        const auto& sc = r.at("scores");
        const auto& st = r.at("steps");
        for (std::size_t p = b; p < e; ++p) {
            for (std::size_t k = 0; k < n; ++k) scores[p * n + k] = sc.at((p - b) * n + k).get<double>();
            steps[p] = st.at(p - b).get<long>();
        }
    };
    try {
        run_range(begin, end);
    } catch (const WorkerFailure&) {
        // Find the culprit(s): retry one program at a time; those that still crash score worst.
        for (std::size_t p = begin; p < end; ++p) {
            try {
                run_range(p, p + 1);
            } catch (const WorkerFailure&) {
                {
                    std::lock_guard<std::mutex> lock(mu_);
                    ++failures_;
                }
                for (std::size_t k = 0; k < n; ++k) scores[p * n + k] = kFailureScore;
                steps[p] = 0;
            }
        }
    }
}

void PluginProblem::evaluate_batch(const MachineSpec& spec, const std::vector<const Program*>& programs, Split split,
                                   const std::vector<std::size_t>& cases, int threads, std::vector<double>& scores,
                                   std::vector<long>& steps) const {
    scores.assign(programs.size() * cases.size(), 0.0);
    steps.assign(programs.size(), 0);
    if (programs.empty()) return;
    const unsigned hw = std::max(1u, std::thread::hardware_concurrency());
    const std::size_t slots = std::clamp<std::size_t>(threads > 0 ? static_cast<std::size_t>(threads) : hw, 1,
                                                      std::min<std::size_t>(programs.size(), 32));
    const std::size_t per = (programs.size() + slots - 1) / slots;
    parallel_for(slots, static_cast<int>(slots), [&](std::size_t s) {
        const std::size_t b = s * per, e = std::min(programs.size(), b + per);
        if (b < e) score_chunk(s, spec, programs, b, e, split, cases, scores, steps);
    });
}

double PluginProblem::evaluate_case(Runner& runner, const Program& program, Split split, std::size_t index) const {
    std::vector<double> sc;
    std::vector<long> st;
    evaluate_batch(runner.spec(), {&program}, split, {index}, 1, sc, st);
    runner.add_steps(st[0]);
    return sc[0];
}

std::pair<std::vector<Metric>, bool> PluginProblem::metrics_and_solved(const MachineSpec& spec, const Program& p, Split s) const {
    for (int attempt = 0; attempt < 2; ++attempt) {
        auto w = acquire(0);
        std::lock_guard<std::mutex> lock(w->m);
        try {
            const json r = request(*w, json{{"op", "metrics"}, {"split", s == Split::Train ? 0 : 1}, {"program", encode(p)}}, &spec);
            std::vector<Metric> ms;
            for (const auto& m : r.at("metrics")) ms.push_back({m.at(0).get<std::string>(), m.at(1).get<double>()});
            return {ms, r.value("solved", false)};
        } catch (const WorkerFailure&) {
            // retried once on a fresh worker; a program that crashes it again has no metrics
        }
    }
    return {{{"plugin_failure", 1.0}}, false};
}

std::vector<Metric> PluginProblem::metrics(const MachineSpec& spec, const Program& program, Split split) const {
    return metrics_and_solved(spec, program, split).first;
}

bool PluginProblem::solved(const MachineSpec& spec, const Program& program, Split split) const {
    return metrics_and_solved(spec, program, split).second;
}

// ------------------------------------------------------------------ building

bool is_plugin_source(const fs::path& p) {
    const auto e = p.extension().string();
    return e == ".cpp" || e == ".cc" || e == ".cxx";
}

BuildResult build_plugin(const fs::path& source) {
    BuildResult res;
    if (!fs::exists(source)) throw std::runtime_error("plug-in source '" + source.string() + "' not found");
    const std::string compiler = [] {
        if (const char* e = std::getenv("ADAPTOGEN_CXX"); e && *e) return std::string(e);
        if (const char* e = std::getenv("CXX"); e && *e) return std::string(e);
        return std::string(ADAPTOGEN_CXX_COMPILER);
    }();
#ifdef __APPLE__
    const std::string ext = ".dylib";
    const std::string shared = "-dynamiclib";
#else
    const std::string ext = ".so";
    const std::string shared = "-shared";
#endif
    const std::string flags = "-std=c++20 -O2 -fPIC -fvisibility=hidden " + shared;
    const fs::path sdk = ADAPTOGEN_SDK_INCLUDE_DIR, json_inc = ADAPTOGEN_JSON_INCLUDE_DIR;
    std::uint64_t h = fnv1a(read_text(source));
    h = fnv1a(compiler + "|" + flags, h);
    h = fnv1a(read_text(sdk / "adaptogen" / "sdk.hpp"), h);
    h = fnv1a(read_text(sdk / "adaptogen" / "plugin_abi.h"), h);
    // Headers next to the source (e.g. a generated base class) are part of the build too.
    for (const auto& f : fs::directory_iterator(fs::absolute(source).parent_path()))
        if (f.path().extension() == ".hpp" || f.path().extension() == ".h") h = fnv1a(read_text(f.path()), h);
    char hex[17];
    std::snprintf(hex, sizeof hex, "%016llx", static_cast<unsigned long long>(h));
    fs::path dir;
    if (const char* c = std::getenv("ADAPTOGEN_CACHE"); c && *c) dir = c;
    else dir = fs::absolute(source).parent_path() / ".adaptogen-build";
    fs::create_directories(dir);
    res.library = dir / (source.stem().string() + "-" + hex + ext);
    if (fs::exists(res.library)) {
        res.ok = res.cached = true;
        return res;
    }
    const fs::path tmp = res.library.string() + ".tmp";
    res.command = shell_quote(compiler) + " " + flags + " -I" + shell_quote(sdk.string()) + " -I" +
                  shell_quote(json_inc.string()) + " -I" + shell_quote(fs::absolute(source).parent_path().string()) + " " +
                  shell_quote(fs::absolute(source).string()) + " -o " + shell_quote(tmp.string());
    FILE* pipe = ::popen((res.command + " 2>&1").c_str(), "r");
    if (!pipe) throw std::runtime_error("cannot run the compiler '" + compiler + "'");
    char buf[4096];
    while (std::fgets(buf, sizeof buf, pipe)) res.output += buf;
    const int status = ::pclose(pipe);
    res.ok = status == 0 && fs::exists(tmp);
    static const std::regex diag(R"(^(.*?):(\d+):(\d+): (fatal error|error|warning|note): (.*)$)");
    std::istringstream lines(res.output);
    for (std::string line; std::getline(lines, line);) {
        std::smatch m;
        if (std::regex_match(line, m, diag))
            res.diagnostics.push_back({m[1].str(), m[4].str() == "fatal error" ? "error" : m[4].str(), m[5].str(),
                                       std::stoi(m[2].str()), std::stoi(m[3].str())});
    }
    if (res.ok) fs::rename(tmp, res.library);
    else fs::remove(tmp);
    return res;
}

void seed_from_parents(const Problem& problem, EngineConfig& cfg) {
    const auto* plugin = dynamic_cast<const PluginProblem*>(&problem);
    if (!plugin || plugin->parents().empty())
        throw std::runtime_error("seeding from parents needs a plug-in problem that derives from strains");
    for (const auto& p : plugin->parents()) cfg.seeding.strains.push_back(p);
    for (const auto& m : plugin->channel_maps()) cfg.seeding.channel_map.push_back(m);
}

std::unique_ptr<PluginProblem> make_plugin_problem(const std::string& path, const json& params, std::uint64_t seed) {
    fs::path lib = path;
    if (is_plugin_source(lib)) {
        const BuildResult b = build_plugin(lib);
        if (!b.ok) {
            std::string msg = "the plug-in '" + path + "' does not compile:\n";
            for (const auto& d : b.diagnostics)
                if (d.severity == "error") msg += "  " + d.file + ":" + std::to_string(d.line) + ":" + std::to_string(d.column) + ": " + d.message + "\n";
            if (b.diagnostics.empty()) msg += b.output;
            msg += "(run `adaptogen plugin validate " + path + "` for details)";
            throw std::runtime_error(msg);
        }
        lib = b.library;
    }
    if (!fs::exists(lib)) throw std::runtime_error("plug-in '" + path + "' not found");
    return std::make_unique<PluginProblem>(lib, params, seed);
}

// ------------------------------------------------------------------ validation

ValidationReport validate_plugin(const fs::path& path, const json& params, std::uint64_t seed, const fs::path& library) {
    ValidationReport rep;
    fs::path lib = path;
    if (is_plugin_source(path)) {
        rep.build = build_plugin(path);
        if (!rep.build.ok) {
            for (const auto& d : rep.build.diagnostics)
                if (d.severity == "error")
                    rep.errors.push_back("compile error " + d.file + ":" + std::to_string(d.line) + ":" + std::to_string(d.column) + ": " + d.message);
            if (rep.errors.empty()) rep.errors.push_back("compilation failed:\n" + rep.build.output);
            return rep;
        }
        lib = rep.build.library;
        rep.notes.push_back(std::string(rep.build.cached ? "using cached build " : "built ") + lib.string());
    }
    std::unique_ptr<PluginProblem> prob;
    try {
        prob = std::make_unique<PluginProblem>(lib, params, seed);
        prob->timeout_seconds = 20;
    } catch (const std::exception& e) {
        rep.errors.push_back(std::string("cannot load the plug-in: ") + e.what());
        return rep;
    }
    rep.info = prob->info();

    // Interface.
    static const std::regex chan_name("^[A-Za-z_][A-Za-z0-9_.]*$");
    std::set<std::pair<std::string, int>> seen;
    const auto chans = prob->channels();
    if (prob->name().empty()) rep.errors.push_back("name() is empty");
    if (chans.empty()) rep.errors.push_back("no channels: programs could neither observe nor do anything");
    bool has_action = false, has_sensor = false;
    for (const auto& c : chans) {
        if (!std::regex_match(c.name, chan_name)) rep.errors.push_back("invalid channel name '" + c.name + "' (letters, digits, '_' and '.')");
        if (c.arity < 0 || c.arity > 3) rep.errors.push_back("channel '" + c.name + "': arity must be 0..3");
        if (!seen.insert({c.name, static_cast<int>(c.kind)}).second) rep.errors.push_back("duplicate channel '" + c.name + "'");
        if (c.description.empty()) rep.warnings.push_back("channel '" + c.name + "' has no description (it helps reuse and transfer)");
        (c.kind == ChannelKind::Action ? has_action : has_sensor) = true;
    }
    if (!has_sensor) rep.warnings.push_back("no sensor channels: programs cannot observe anything");
    if (prob->num_outputs() < 0 || prob->num_outputs() > 8) rep.errors.push_back("num_outputs must be 0..8");
    if (prob->num_outputs() == 0 && !has_action) rep.errors.push_back("no outputs and no actions: programs cannot do anything");
    if (prob->num_cases(Split::Train) == 0) rep.errors.push_back("no training cases");
    if (prob->num_cases(Split::Test) == 0) rep.errors.push_back("no test cases (needed to measure generalisation)");
    else if (prob->num_cases(Split::Test) < 10) rep.warnings.push_back("fewer than 10 test cases: generalisation estimates will be noisy");

    // Inheritance rules.
    const auto parents = prob->parents();
    const auto maps = prob->channel_maps();
    std::set<std::string> disabled;
    for (const auto& d : rep.info.value("disabled_channels", json::array())) disabled.insert(d.get<std::string>());
    StrainLibrary lib_bank(library);
    for (const auto& pid : parents) {
        Strain s;
        try {
            s = lib_bank.load(pid);
        } catch (const std::exception&) {
            rep.warnings.push_back("parent strain '" + pid + "' is not in library '" + library.string() + "': inheritance not checked");
            continue;
        }
        for (const auto& pc : s.channels) {
            std::string target = pc.name;
            for (const auto& [k, v] : maps)
                if (k == s.id + ":" + pc.name || k == s.name + ":" + pc.name) target = v;
            if (disabled.count(target) || disabled.count(pc.name)) continue;
            const Channel* mine = nullptr;
            for (const auto& c : chans)
                if (c.name == target && c.kind == pc.kind) mine = &c;
            if (!mine) {
                rep.errors.push_back("inherited channel '" + target + "' (from " + s.id + ") is missing; declare it in disabled_channels() if unsupported");
            } else if (mine->arity != pc.arity || mine->units != pc.units || mine->description != pc.description) {
                rep.errors.push_back("inherited channel '" + target + "' (from " + s.id +
                                     ") is redefined: inherited channels must keep their arity, units and description");
            }
        }
        rep.notes.push_back("inherits from " + s.id);
    }

    if (!rep.errors.empty()) return rep;

    // Behaviour: random programs must get finite, deterministic scores; a crash or hang is reported.
    EngineConfig cfg;
    const MachineSpec spec = make_machine(*prob, cfg);
    Rng rng(seed ^ 0xc4ec4ULL);
    std::vector<Program> programs(1);  // the empty program first
    for (int i = 0; i < 20; ++i) programs.push_back(random_program(spec, cfg.variation, rng));
    std::vector<const Program*> ptrs;
    for (const auto& p : programs) ptrs.push_back(&p);
    for (Split s : {Split::Train, Split::Test}) {
        std::vector<std::size_t> cases;
        for (std::size_t i = 0; i < std::min<std::size_t>(10, prob->num_cases(s)); ++i) cases.push_back(i);
        if (cases.empty()) continue;
        std::vector<double> a, b;
        std::vector<long> sa, sb;
        const long before = prob->failures();
        prob->evaluate_batch(spec, ptrs, s, cases, 2, a, sa);
        prob->evaluate_batch(spec, ptrs, s, cases, 2, b, sb);
        if (prob->failures() > before)
            rep.errors.push_back(std::to_string(prob->failures() - before) +
                                 " random program(s) crashed or hung the plug-in on " + (s == Split::Train ? "training" : "test") + " cases");
        int nonfinite = 0;
        for (double v : a) nonfinite += v <= PluginProblem::kFailureScore;
        if (nonfinite > 0 && prob->failures() == before)
            rep.errors.push_back("evaluate_case returned NaN/inf (or crashed) for " + std::to_string(nonfinite) + " evaluations");
        if (a != b) rep.errors.push_back("evaluate_case is not deterministic: the same program got different scores");
        std::set<double> distinct(a.begin(), a.end());
        if (s == Split::Train && distinct.size() == 1)
            rep.warnings.push_back("every program gets the same score: evolution has nothing to select on (use graded scores)");
    }
    try {
        const auto ms = prob->metrics(spec, programs[1], Split::Test);
        if (ms.empty()) rep.warnings.push_back("metrics() returned nothing");
        else rep.notes.push_back("headline metric: " + ms[0].name);
        (void)prob->solved(spec, programs[1], Split::Test);
    } catch (const std::exception& e) {
        rep.errors.push_back(std::string("metrics()/solved() failed: ") + e.what());
    }
    // How well the parents' behaviour already does here.
    for (const auto& pid : parents) {
        try {
            const Strain s = lib_bank.load(pid);
            ChannelMap cm;
            for (const auto& [k, v] : maps)
                if (k.rfind(s.id + ":", 0) == 0 || k.rfind(s.name + ":", 0) == 0) cm.emplace_back(k.substr(k.find(':') + 1), v);
            const Program p = revive(s, spec, cm);
            const auto ms = prob->metrics(spec, p, Split::Test);
            if (!ms.empty()) {
                char buf[128];
                std::snprintf(buf, sizeof buf, "parent %s's behaviour scores %s = %.4g here", s.id.c_str(), ms[0].name.c_str(), ms[0].value);
                rep.notes.push_back(buf);
            }
        } catch (const std::exception&) {
        }
    }
    rep.ok = rep.errors.empty();
    return rep;
}

// ------------------------------------------------------------------ base classes from strains

namespace {


std::string literal(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        if (c == '\n') {
            out += "\\n";
            continue;
        }
        out += c;
    }
    return out + "\"";
}

}  // namespace

std::string generate_base_header(const std::vector<Strain>& parents, const std::string& class_name,
                                 const std::vector<std::pair<std::string, std::string>>& renames) {
    if (parents.empty()) throw std::runtime_error("at least one parent strain is needed");
    if (cpp_identifier(class_name) != class_name) throw std::runtime_error("'" + class_name + "' is not a valid C++ class name");
    struct Merged {
        Channel c;
        std::vector<std::string> origins;
    };
    std::vector<Merged> merged;
    std::vector<std::string> conflicts;
    std::vector<std::pair<std::string, std::string>> maps;
    int outputs = 0;
    for (const auto& s : parents) {
        outputs = std::max(outputs, s.num_outputs);
        for (const auto& c0 : s.channels) {
            Channel c = c0;
            for (const auto& [k, v] : renames)
                if (k == s.id + ":" + c0.name || k == s.name + ":" + c0.name) {
                    c.name = v;
                    maps.emplace_back(s.id + ":" + c0.name, v);
                }
            auto it = std::find_if(merged.begin(), merged.end(), [&](const Merged& m) { return m.c.name == c.name && m.c.kind == c.kind; });
            if (it == merged.end()) {
                merged.push_back({c, {s.id}});
            } else if (it->c.arity == c.arity && it->c.units == c.units && it->c.description == c.description) {
                it->origins.push_back(s.id);  // the same channel inherited twice: merged
            } else {
                conflicts.push_back("channel '" + c.name + "' differs between " + it->origins.front() + " and " + s.id +
                                    " (rename one, e.g. --rename " + s.id + ":" + c0.name + "=" + s.name + "." + c0.name + ")");
            }
        }
    }
    if (!conflicts.empty()) {
        std::string msg = "cannot merge the parents' interfaces:\n";
        for (const auto& c : conflicts) msg += "  " + c + "\n";
        throw std::runtime_error(msg);
    }
    std::ostringstream h;
    std::string ids;
    for (const auto& s : parents) ids += (ids.empty() ? "" : ", ") + s.id;
    h << "// Generated by `adaptogen plugin header` from strain(s): " << ids << "\n"
      << "// Do not edit: regenerate instead. Derive your problem from " << class_name << ".\n"
      << "//\n"
      << "// The inherited channels are fixed (channels() is final): a derived problem keeps their\n"
      << "// meaning so the parents' evolved code stays valid. Add your own channels in\n"
      << "// extra_channels(); their indices start at kInheritedChannels. To drop an inherited channel,\n"
      << "// list it in disabled_channels(); code using it becomes inactive (a pseudogene).\n"
      << "#pragma once\n#include <adaptogen/sdk.hpp>\n\n"
      << "class " << class_name << " : public adaptogen::sdk::Problem {\npublic:\n";
    std::set<std::string> used_ids;
    for (std::size_t i = 0; i < merged.size(); ++i) {
        std::string id = cpp_identifier(merged[i].c.name);
        while (!used_ids.insert(id).second) id += "_";
        const Channel& c = merged[i].c;
        h << "    static constexpr int " << id << " = " << i << ";  // " << to_string(c.kind) << ", arity " << c.arity
          << (c.units.empty() ? "" : " [" + c.units + "]") << ": " << c.description << "\n";
    }
    h << "    static constexpr int kInheritedChannels = " << merged.size() << ";\n\n"
      << "    static std::vector<adaptogen::sdk::Channel> inherited_channels() {\n        return {\n";
    for (const auto& m : merged)
        h << "            {" << literal(m.c.name) << ", adaptogen::sdk::Kind::"
          << (m.c.kind == ChannelKind::Sensor ? "Sensor" : "Action") << ", " << m.c.arity << ", " << literal(m.c.description)
          << ", " << literal(m.c.units) << "},\n";
    h << "        };\n    }\n"
      << "    /// Channels this problem adds (indices kInheritedChannels, kInheritedChannels + 1, ...).\n"
      << "    virtual std::vector<adaptogen::sdk::Channel> extra_channels() const { return {}; }\n\n"
      << "    std::vector<adaptogen::sdk::Channel> channels() const final {\n"
      << "        auto all = inherited_channels();\n        const auto extra = extra_channels();\n"
      << "        all.insert(all.end(), extra.begin(), extra.end());\n        return all;\n    }\n"
      << "    std::vector<std::string> parents() const final { return {";
    for (std::size_t i = 0; i < parents.size(); ++i) h << (i ? ", " : "") << literal(parents[i].id);
    h << "}; }\n"
      << "    std::vector<std::pair<std::string, std::string>> channel_maps() const final { return {";
    for (std::size_t i = 0; i < maps.size(); ++i) h << (i ? ", " : "") << "{" << literal(maps[i].first) << ", " << literal(maps[i].second) << "}";
    h << "}; }\n"
      << "    int num_outputs() const override { return " << outputs << "; }\n"
      << "};\n";
    return h.str();
}

std::string plugin_template(const std::string& problem_name, const std::string& class_name,
                            const std::string& base_header, const std::string& base_class) {
    std::ostringstream t;
    t << "// adaptogen plug-in: " << problem_name << "\n"
      << "//\n"
      << "// Build and check:  adaptogen plugin validate " << problem_name << ".cpp\n"
      << "// Evolve:           adaptogen evolve --problem plugin:" << problem_name << ".cpp\n"
      << "//\n"
      << "// Rules: evaluate_case must be deterministic; higher scores are better; give partial credit\n"
      << "// (graded scores) so evolution has stepping stones; keep test cases separate from training.\n";
    if (!base_header.empty()) t << "#include \"" << base_header << "\"\n";
    t << "#include <adaptogen/sdk.hpp>\n\nusing namespace adaptogen::sdk;\n\n";
    const std::string base = base_class.empty() ? "Problem" : base_class;
    t << "class " << class_name << " : public " << base << " {\npublic:\n"
      << "    " << class_name << "(const Params& params, std::uint64_t seed) {\n"
      << "        // Generate cases from the seed so runs are reproducible.\n"
      << "        Rng rng(seed);\n"
      << "        const int n_train = params.get(\"train_cases\", 50), n_test = params.get(\"test_cases\", 100);\n"
      << "        for (int i = 0; i < n_train + n_test; ++i) {\n"
      << "            Case c;\n            c.x = rng.uniform(-10, 10);\n"
      << "            (i < n_train ? train_ : test_).push_back(c);\n        }\n    }\n\n"
      << "    std::string name() const override { return \"" << problem_name << "\"; }\n"
      << "    std::string description() const override { return \"TODO: describe the problem\"; }\n\n";
    if (base_class.empty()) {
        t << "    // What programs can observe (sensors) and do (actions). Indices follow this order.\n"
          << "    std::vector<Channel> channels() const override { return {sensor(\"x\", 0, \"the input value\")}; }\n";
    } else {
        t << "    // Inherited channels come first (see the generated header); add new ones here.\n"
          << "    std::vector<Channel> extra_channels() const override { return {sensor(\"x\", 0, \"the input value\")}; }\n";
    }
    t << "    std::size_t num_cases(Split s) const override { return s == Split::Train ? train_.size() : test_.size(); }\n\n"
      << "    double evaluate_case(Runner& run, Split s, std::size_t i) const override {\n"
      << "        const Case& c = (s == Split::Train ? train_ : test_)[i];\n"
      << "        // Answer the program's sensor reads (and perform its actions, if any).\n";
    if (base_class.empty())
        t << "        auto env = make_env([&](int channel, const double*, int) { return channel == 0 ? c.x : 0.0; });\n";
    else
        t << "        auto env = make_env([&](int channel, const double*, int) {\n"
          << "            if (channel == kInheritedChannels) return c.x;  // our first extra channel\n"
          << "            return 0.0;  // TODO: answer the inherited channels\n        });\n";
    t << "        run(env);\n"
      << "        // TODO: score the outcome. Example: the program should output 2x in register r0.\n"
      << "        const double target = 2 * c.x;\n"
      << "        return -std::fabs(run.reg(0) - target);\n    }\n\n"
      << "    std::vector<Metric> metrics(const Evaluator& eval, Split s) const override {\n"
      << "        double err = 0;\n        const std::size_t n = num_cases(s);\n"
      << "        for (std::size_t i = 0; i < n; ++i) err -= eval.score(s, i);\n"
      << "        return {{\"mean_abs_error\", n ? err / static_cast<double>(n) : 0.0}};\n    }\n"
      << "    bool solved(const Evaluator& eval, Split s) const override { return metrics(eval, s)[0].value < 1e-6; }\n\n"
      << "private:\n    struct Case {\n        double x = 0;\n    };\n    std::vector<Case> train_, test_;\n};\n\n"
      << "ADAPTOGEN_PLUGIN(" << class_name << ")\n";
    return t.str();
}

}  // namespace adaptogen
