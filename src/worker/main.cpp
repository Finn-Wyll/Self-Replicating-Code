// adaptogen-worker: hosts one plug-in problem in its own process.
//
// The engine talks to it over stdin/stdout, one JSON message per line:
//   {"op":"init","plugin":PATH,"params":{...},"seed":N}  -> {"ok":true,"info":{...}}
//   {"op":"machine","registers":R,"ops":[...],"limits":{...},"const_min":a,"const_max":b} -> {"ok":true}
//   {"op":"score","split":0|1,"cases":[...],"programs":[[[op,dst,a,b,c,channel,imm],...],...]}
//        -> {"ok":true,"scores":[...programs x cases...],"steps":[...],"errors":k}
//   {"op":"metrics","split":0|1,"program":[...]} -> {"ok":true,"metrics":{...},"solved":bool}
//   {"op":"quit"}
// A crash or hang of the plug-in kills only this process; the engine restarts it.
#include <dlfcn.h>

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "adaptogen/plugin_abi.h"
#include "adaptogen/vm.hpp"

using nlohmann::json;
using namespace adaptogen;

namespace {

struct HostCtx {
    const MachineSpec* spec;
    const Program* program;
};

struct AbiEnv : Environment {
    const ag_env* e;
    explicit AbiEnv(const ag_env* env) : e(env) {}
    double sense(int ch, const double* a, int n) override { return e->sense ? e->sense(e->self, ch, a, n) : 0.0; }
    double act(int ch, const double* a, int n) override { return e->act ? e->act(e->self, ch, a, n) : 0.0; }
};

int host_run(void* ctx, const ag_env* env, double* regs, int nregs) {
    auto* h = static_cast<HostCtx*>(ctx);
    AbiEnv adapter(env);
    std::vector<double> tmp;
    double* r = regs;
    if (nregs < h->spec->num_registers) {  // never let the VM write past the caller's array
        tmp.assign(regs, regs + nregs);
        tmp.resize(static_cast<std::size_t>(h->spec->num_registers), 0.0);
        r = tmp.data();
    }
    const RunStats st = run(*h->spec, *h->program, adapter, std::span<double>(r, static_cast<std::size_t>(h->spec->num_registers)));
    if (r != regs) std::copy(tmp.begin(), tmp.begin() + nregs, regs);
    return st.steps;
}

Program decode(const json& j) {
    Program p;
    for (const auto& x : j) {
        Instr in;
        const int op = x.at(0).get<int>();
        in.op = op >= 0 && op < kNumOps ? static_cast<Op>(op) : Op::Nop;
        in.dst = x.at(1).get<std::uint8_t>();
        in.a = x.at(2).get<std::uint8_t>();
        in.b = x.at(3).get<std::uint8_t>();
        in.c = x.at(4).get<std::uint8_t>();
        in.channel = x.at(5).get<std::uint16_t>();
        in.imm = x.at(6).get<double>();
        p.code.push_back(in);
    }
    return p;
}

void reply(const json& j) { std::cout << j.dump() << '\n' << std::flush; }

}  // namespace

int main() {
    std::ios::sync_with_stdio(false);
    const ag_plugin_v1* api = nullptr;
    void* problem = nullptr;
    MachineSpec spec;
    bool have_machine = false;
    json info;

    for (std::string line; std::getline(std::cin, line);) {
        json req;
        try {
            req = json::parse(line);
        } catch (const std::exception& e) {
            reply({{"ok", false}, {"error", std::string("bad request: ") + e.what()}});
            continue;
        }
        const std::string op = req.value("op", "");
        try {
            if (op == "quit") break;
            if (op == "init") {
                const std::string path = req.at("plugin").get<std::string>();
                void* lib = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
                if (!lib) throw std::runtime_error(std::string("cannot load plug-in: ") + dlerror());
                auto entry = reinterpret_cast<ag_plugin_entry_v1>(dlsym(lib, ADAPTOGEN_PLUGIN_ENTRY));
                if (!entry) throw std::runtime_error("not an adaptogen plug-in: no " ADAPTOGEN_PLUGIN_ENTRY "() (missing ADAPTOGEN_PLUGIN(...)?)");
                api = entry();
                if (!api || api->abi_version != ADAPTOGEN_PLUGIN_ABI_VERSION)
                    throw std::runtime_error("plug-in ABI version mismatch (expected " + std::to_string(ADAPTOGEN_PLUGIN_ABI_VERSION) + ")");
                char err[1024] = {0};
                const std::string params = req.value("params", json::object()).dump();
                problem = api->create(params.c_str(), req.value("seed", 1ULL), err, sizeof err);
                if (!problem) throw std::runtime_error(std::string("problem constructor failed: ") + (err[0] ? err : "no message"));
                info = json::parse(api->info(problem));
                reply({{"ok", true}, {"info", info}});
            } else if (op == "machine") {
                if (!problem) throw std::runtime_error("init first");
                spec = MachineSpec{};
                spec.num_registers = req.at("registers").get<int>();
                for (const auto& o : req.at("ops")) spec.ops.push_back(static_cast<Op>(o.get<int>()));
                const json& lim = req.at("limits");
                spec.limits.max_steps = lim.at("max_steps").get<int>();
                spec.limits.max_loop_iterations = lim.at("max_loop_iterations").get<int>();
                spec.limits.max_loop_body = lim.at("max_loop_body").get<int>();
                spec.const_min = req.value("const_min", -5.0);
                spec.const_max = req.value("const_max", 5.0);
                for (const auto& c : info.at("channels"))
                    spec.channels.push_back({c.at("name").get<std::string>(),
                                             c.at("kind").get<std::string>() == "action" ? ChannelKind::Action : ChannelKind::Sensor,
                                             c.at("arity").get<int>(), c.value("description", ""), c.value("units", "")});
                spec.finalize();
                have_machine = true;
                reply({{"ok", true}});
            } else if (op == "score") {
                if (!have_machine) throw std::runtime_error("send the machine first");
                const int split = req.at("split").get<int>();
                const auto cases = req.at("cases").get<std::vector<unsigned long long>>();
                const json& progs = req.at("programs");
                std::vector<double> scores;
                std::vector<long> steps;
                int errors = 0;
                scores.reserve(progs.size() * cases.size());
                for (const auto& pj : progs) {
                    const Program prog = decode(pj);
                    HostCtx ctx{&spec, &prog};
                    long total_steps = 0;
                    // Count steps by wrapping run.
                    struct Counting {
                        static int run(void* c, const ag_env* e, double* r, int n) {
                            auto* pair = static_cast<std::pair<HostCtx*, long*>*>(c);
                            const int s = host_run(pair->first, e, r, n);
                            *pair->second += s;
                            return s;
                        }
                    };
                    std::pair<HostCtx*, long*> pc{&ctx, &total_steps};
                    ag_host host{&pc, spec.num_registers, &Counting::run};
                    for (unsigned long long c : cases) {
                        double v = api->evaluate_case(problem, &host, split, c);
                        if (!std::isfinite(v)) {
                            ++errors;
                            v = -1e12;  // a NaN/inf score counts as the worst possible outcome
                        }
                        scores.push_back(v);
                    }
                    steps.push_back(total_steps);
                }
                reply({{"ok", true}, {"scores", scores}, {"steps", steps}, {"errors", errors}});
            } else if (op == "metrics") {
                if (!have_machine) throw std::runtime_error("send the machine first");
                const int split = req.at("split").get<int>();
                const Program prog = decode(req.at("program"));
                HostCtx ctx{&spec, &prog};
                ag_host host{&ctx, spec.num_registers, &host_run};
                std::vector<char> buf(4096);
                int n = api->metrics(problem, &host, split, buf.data(), static_cast<int>(buf.size()));
                if (n >= static_cast<int>(buf.size())) {
                    buf.resize(static_cast<std::size_t>(n) + 1);
                    n = api->metrics(problem, &host, split, buf.data(), static_cast<int>(buf.size()));
                }
                if (n < 0) throw std::runtime_error("metrics() failed in the plug-in");
                const int solved = api->solved(problem, &host, split);
                if (solved < 0) throw std::runtime_error("solved() failed in the plug-in");
                json ordered = json::array();  // [[name, value], ...] keeps the plug-in's order
                const auto parsed = nlohmann::ordered_json::parse(buf.data());  // must outlive the loop
                for (const auto& [k, v] : parsed.items()) ordered.push_back({k, v.get<double>()});
                reply({{"ok", true}, {"metrics", ordered}, {"solved", solved == 1}});
            } else {
                throw std::runtime_error("unknown op '" + op + "'");
            }
        } catch (const std::exception& e) {
            reply({{"ok", false}, {"error", e.what()}});
        }
    }
    if (api && problem) api->destroy(problem);
    return 0;
}
