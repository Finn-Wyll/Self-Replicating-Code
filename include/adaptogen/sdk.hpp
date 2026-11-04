#pragma once
// adaptogen plug-in SDK: everything needed to write a new problem as a plug-in.
//
// A plug-in is one .cpp file:
//
//     #include <adaptogen/sdk.hpp>
//     using namespace adaptogen::sdk;
//
//     class MyProblem : public Problem {
//     public:
//         MyProblem(const Params& params, std::uint64_t seed) { ... generate cases from seed ... }
//         std::string name() const override { return "my-problem"; }
//         std::string description() const override { return "what it is about"; }
//         std::vector<Channel> channels() const override {
//             return {sensor("price", 0, "today's price", "EUR"), action("buy", 1, "buy n units", "units")};
//         }
//         std::size_t num_cases(Split s) const override { return s == Split::Train ? 100 : 50; }
//         double evaluate_case(Runner& run, Split s, std::size_t i) const override {
//             ... build an environment for case i, call run(env), read run.reg(0), return a score ...
//         }
//     };
//     ADAPTOGEN_PLUGIN(MyProblem)
//
// Build and check it with:  adaptogen plugin validate my_problem.cpp
// Use it with:              adaptogen evolve --problem plugin:my_problem.cpp
//
// Rules: evaluate_case must be deterministic and thread-compatible (const, no shared mutable
// state); higher scores are better; prefer graded scores (partial credit) over pass/fail.
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "adaptogen/plugin_abi.h"

namespace adaptogen::sdk {

enum class Split { Train = 0, Test = 1 };
enum class Kind { Sensor, Action };

struct Channel {
    std::string name;  // e.g. "item.duration"; letters, digits, '_' and '.'
    Kind kind = Kind::Sensor;
    int arity = 0;     // sensors: number of index arguments; actions: number of arguments (0..3)
    std::string description;
    std::string units;
    bool operator==(const Channel&) const = default;
};

inline Channel sensor(std::string name, int arity, std::string description, std::string units = "") {
    return {std::move(name), Kind::Sensor, arity, std::move(description), std::move(units)};
}
inline Channel action(std::string name, int arity, std::string description, std::string units = "") {
    return {std::move(name), Kind::Action, arity, std::move(description), std::move(units)};
}

struct Metric {
    std::string name;
    double value = 0.0;
};

/// Problem parameters (a JSON object), e.g. params.get("tasks", 10).
class Params {
public:
    explicit Params(nlohmann::json j = nlohmann::json::object()) : j_(std::move(j)) {}
    template <class T>
    T get(const std::string& key, T fallback) const {
        return j_.contains(key) ? j_.at(key).get<T>() : fallback;
    }
    bool has(const std::string& key) const { return j_.contains(key); }
    const nlohmann::json& json() const { return j_; }

private:
    nlohmann::json j_;
};

/// Deterministic random numbers (same sequence on every platform).
class Rng {
public:
    explicit Rng(std::uint64_t seed = 1) {
        std::uint64_t x = seed;
        for (auto& s : s_) {
            std::uint64_t z = (x += 0x9e3779b97f4a7c15ULL);
            z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
            z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
            s = z ^ (z >> 31);
        }
    }
    std::uint64_t next() {
        auto rotl = [](std::uint64_t v, int k) { return (v << k) | (v >> (64 - k)); };
        const std::uint64_t r = rotl(s_[1] * 5, 7) * 9, t = s_[1] << 17;
        s_[2] ^= s_[0];
        s_[3] ^= s_[1];
        s_[1] ^= s_[2];
        s_[0] ^= s_[3];
        s_[2] ^= t;
        s_[3] = rotl(s_[3], 45);
        return r;
    }
    double uniform() { return static_cast<double>(next() >> 11) * 0x1.0p-53; }
    double uniform(double lo, double hi) { return lo + (hi - lo) * uniform(); }
    int range(int lo, int hi) {  // inclusive
        return hi <= lo ? lo : lo + static_cast<int>(next() % static_cast<std::uint64_t>(hi - lo + 1));
    }
    bool chance(double p) { return uniform() < p; }

private:
    std::uint64_t s_[4]{};
};

/// What a program can observe and do in one case: implement sense() and/or act().
class Environment {
public:
    virtual ~Environment() = default;
    virtual double sense(int channel, const double* args, int nargs) {
        (void)channel; (void)args; (void)nargs;
        return 0.0;
    }
    virtual double act(int channel, const double* args, int nargs) {
        (void)channel; (void)args; (void)nargs;
        return 0.0;
    }
};

/// Environment from two callables: make_env([&](int ch, const double* a, int n) {...}, ...).
template <class S, class A = std::nullptr_t>
class LambdaEnv : public Environment {
public:
    LambdaEnv(S s, A a) : s_(std::move(s)), a_(std::move(a)) {}
    double sense(int ch, const double* args, int n) override { return s_(ch, args, n); }
    double act(int ch, const double* args, int n) override {
        if constexpr (std::is_same_v<A, std::nullptr_t>) return 0.0;
        else return a_(ch, args, n);
    }

private:
    S s_;
    A a_;
};
template <class S, class A = std::nullptr_t>
LambdaEnv<S, A> make_env(S s, A a = nullptr) {
    return LambdaEnv<S, A>(std::move(s), std::move(a));
}

/// Runs the evolved program. Registers persist between calls within one evaluate_case, so a
/// control problem can call run() once per time step; call reset() to clear them.
class Runner {
public:
    explicit Runner(const ag_host* host) : host_(host), regs_(static_cast<std::size_t>(host->registers), 0.0) {}
    int run(Environment& env) {
        ag_env e{&env, &Runner::sense_cb, &Runner::act_cb};
        const int steps = host_->run(host_->ctx, &e, regs_.data(), static_cast<int>(regs_.size()));
        steps_ += steps;
        return steps;
    }
    int operator()(Environment& env) { return run(env); }
    double reg(int i) const { return i >= 0 && i < static_cast<int>(regs_.size()) ? regs_[static_cast<std::size_t>(i)] : 0.0; }
    void set_reg(int i, double v) {
        if (i >= 0 && i < static_cast<int>(regs_.size())) regs_[static_cast<std::size_t>(i)] = v;
    }
    void reset() { std::fill(regs_.begin(), regs_.end(), 0.0); }
    long steps() const { return steps_; }

private:
    static double sense_cb(void* self, int ch, const double* a, int n) { return static_cast<Environment*>(self)->sense(ch, a, n); }
    static double act_cb(void* self, int ch, const double* a, int n) { return static_cast<Environment*>(self)->act(ch, a, n); }
    const ag_host* host_;
    std::vector<double> regs_;
    long steps_ = 0;
};

class Problem;

/// Lets metrics()/solved() score the current program on any case.
class Evaluator {
public:
    Evaluator(const Problem& p, const ag_host* host) : p_(p), host_(host) {}
    double score(Split s, std::size_t i) const;
    const ag_host* host() const { return host_; }

private:
    const Problem& p_;
    const ag_host* host_;
};

class Problem {
public:
    virtual ~Problem() = default;
    virtual std::string name() const = 0;
    virtual std::string description() const = 0;
    virtual std::vector<Channel> channels() const = 0;
    /// Registers r0..r(n-1) read as the program's answer (0 for problems driven by actions).
    virtual int num_outputs() const { return 1; }
    /// Instruction set suggestion (op names); empty = the engine's general-purpose set.
    virtual std::vector<std::string> default_ops() const { return {}; }
    virtual std::size_t num_cases(Split s) const = 0;
    /// Score the program on one case; higher is better. Must be deterministic.
    virtual double evaluate_case(Runner& run, Split s, std::size_t index) const = 0;
    /// Human-readable metrics; the first one is the headline. Default: mean case score.
    virtual std::vector<Metric> metrics(const Evaluator& eval, Split s) const {
        double total = 0;
        const std::size_t n = num_cases(s);
        for (std::size_t i = 0; i < n; ++i) total += eval.score(s, i);
        return {{"mean_score", n ? total / static_cast<double>(n) : 0.0}};
    }
    /// Whether the program counts as a full solution (early stopping, success rates).
    virtual bool solved(const Evaluator& eval, Split s) const {
        (void)eval; (void)s;
        return false;
    }

    // Inheritance from strains (filled in by generated base classes; see `adaptogen plugin header`).
    virtual std::vector<std::string> parents() const { return {}; }
    /// Inherited channels this problem does not support; code using them becomes pseudogenes.
    virtual std::vector<std::string> disabled_channels() const { return {}; }
    /// Renamed inherited channels: {"strain-id:old.name", "new.name"} (multiple-inheritance conflicts).
    virtual std::vector<std::pair<std::string, std::string>> channel_maps() const { return {}; }
};

inline double Evaluator::score(Split s, std::size_t i) const {
    Runner r(host_);
    return p_.evaluate_case(r, s, i);
}

// ------------------------------------------------------------------ ABI glue (do not use directly)
namespace detail {

inline std::string info_json(const Problem& p) {
    nlohmann::json chans = nlohmann::json::array();
    for (const auto& c : p.channels())
        chans.push_back({{"name", c.name},
                         {"kind", c.kind == Kind::Sensor ? "sensor" : "action"},
                         {"arity", c.arity},
                         {"description", c.description},
                         {"units", c.units}});
    nlohmann::json maps = nlohmann::json::object();
    for (const auto& [k, v] : p.channel_maps()) maps[k] = v;
    return nlohmann::json{{"name", p.name()},
                          {"description", p.description()},
                          {"channels", chans},
                          {"num_outputs", p.num_outputs()},
                          {"default_ops", p.default_ops()},
                          {"cases", {{"train", p.num_cases(Split::Train)}, {"test", p.num_cases(Split::Test)}}},
                          {"parents", p.parents()},
                          {"disabled_channels", p.disabled_channels()},
                          {"channel_maps", maps}}
        .dump();
}

struct Instance {
    std::unique_ptr<Problem> problem;
    std::string info;
};

inline void write_error(char* err, int errlen, const std::string& msg) {
    if (!err || errlen <= 0) return;
    std::strncpy(err, msg.c_str(), static_cast<std::size_t>(errlen - 1));
    err[errlen - 1] = '\0';
}

template <class P>
const ag_plugin_v1* make_table() {
    static const ag_plugin_v1 table = {
        ADAPTOGEN_PLUGIN_ABI_VERSION,
        // create
        [](const char* params, unsigned long long seed, char* err, int errlen) -> void* {
            try {
                auto inst = std::make_unique<Instance>();
                inst->problem = std::make_unique<P>(Params(nlohmann::json::parse(params ? params : "{}")),
                                                    static_cast<std::uint64_t>(seed));
                inst->info = info_json(*inst->problem);
                return inst.release();
            } catch (const std::exception& e) {
                write_error(err, errlen, e.what());
            } catch (...) {
                write_error(err, errlen, "unknown exception in problem constructor");
            }
            return nullptr;
        },
        // destroy
        [](void* p) { delete static_cast<Instance*>(p); },
        // info
        [](void* p) -> const char* { return static_cast<Instance*>(p)->info.c_str(); },
        // evaluate_case
        [](void* p, const ag_host* host, int split, unsigned long long index) -> double {
            try {
                Runner r(host);
                return static_cast<Instance*>(p)->problem->evaluate_case(r, static_cast<Split>(split),
                                                                         static_cast<std::size_t>(index));
            } catch (...) {
                return std::nan("");
            }
        },
        // metrics
        [](void* p, const ag_host* host, int split, char* buf, int buflen) -> int {
            try {
                const Problem& prob = *static_cast<Instance*>(p)->problem;
                nlohmann::ordered_json j = nlohmann::ordered_json::object();  // keep order: first = headline
                for (const auto& m : prob.metrics(Evaluator(prob, host), static_cast<Split>(split))) j[m.name] = m.value;
                const std::string s = j.dump();
                if (static_cast<int>(s.size()) < buflen) std::memcpy(buf, s.c_str(), s.size() + 1);
                return static_cast<int>(s.size());
            } catch (...) {
                return -1;
            }
        },
        // solved
        [](void* p, const ag_host* host, int split) -> int {
            try {
                const Problem& prob = *static_cast<Instance*>(p)->problem;
                return prob.solved(Evaluator(prob, host), static_cast<Split>(split)) ? 1 : 0;
            } catch (...) {
                return -1;
            }
        },
    };
    return &table;
}

}  // namespace detail
}  // namespace adaptogen::sdk

/// Export a problem class (constructor: (const Params&, std::uint64_t seed)) as the plug-in.
#define ADAPTOGEN_PLUGIN(ProblemClass)                                                         \
    extern "C" __attribute__((visibility("default"))) const ag_plugin_v1* adaptogen_plugin_v1() { \
        return ::adaptogen::sdk::detail::make_table<ProblemClass>();                         \
    }
