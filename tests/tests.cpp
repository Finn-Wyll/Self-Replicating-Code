// Minimal self-contained test runner (no external framework needed).
#include <array>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "adaptogen/analysis.hpp"
#include "adaptogen/engine.hpp"
#include "adaptogen/problems/formula.hpp"
#include "adaptogen/registry.hpp"
#include "adaptogen/serialize.hpp"

using namespace adaptogen;

namespace {
int failures = 0, checks = 0;
std::vector<std::pair<std::string, std::function<void()>>>& tests() {
    static std::vector<std::pair<std::string, std::function<void()>>> t;
    return t;
}
struct Reg {
    Reg(const char* n, std::function<void()> f) { tests().emplace_back(n, std::move(f)); }
};
#define TEST(name) \
    static void name(); \
    static Reg reg_##name(#name, name); \
    static void name()
#define CHECK(cond)                                                                 \
    do {                                                                            \
        ++checks;                                                                   \
        if (!(cond)) {                                                              \
            ++failures;                                                             \
            std::printf("  FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond);         \
        }                                                                           \
    } while (0)
#define CHECK_NEAR(a, b, tol) CHECK(std::fabs((a) - (b)) <= (tol))

struct XEnv : Environment {
    std::vector<double> x;
    double sense(int ch, const double*, int) override { return x[static_cast<std::size_t>(ch)]; }
};

MachineSpec spec_with(std::vector<Channel> ch) {
    MachineSpec s;
    s.ops = core_ops();
    s.channels = std::move(ch);
    s.finalize();
    return s;
}

Instr I(Op op, int dst = 0, int a = 0, int b = 0, int c = 0, double imm = 0, int channel = 0) {
    Instr in;
    in.op = op;
    in.dst = static_cast<std::uint8_t>(dst);
    in.a = static_cast<std::uint8_t>(a);
    in.b = static_cast<std::uint8_t>(b);
    in.c = static_cast<std::uint8_t>(c);
    in.imm = imm;
    in.channel = static_cast<std::uint16_t>(channel);
    return in;
}

double run1(const MachineSpec& s, const Program& p, std::vector<double> x, RunStats* st = nullptr) {
    XEnv env;
    env.x = std::move(x);
    std::array<double, 8> r{};
    auto stats = run(s, p, env, r);
    if (st) *st = stats;
    return r[0];
}
}  // namespace

TEST(rng_is_deterministic) {
    Rng a(42), b(42), c(43);
    bool same = true, differ = false;
    for (int i = 0; i < 100; ++i) {
        auto x = a.next(), y = b.next(), z = c.next();
        same = same && x == y;
        differ = differ || x != z;
    }
    CHECK(same);
    CHECK(differ);
    Rng r(1);
    for (int i = 0; i < 1000; ++i) {
        int v = r.range(3, 7);
        CHECK(v >= 3 && v <= 7);
    }
}

TEST(vm_arithmetic_and_sensors) {
    auto s = spec_with({{"x", ChannelKind::Sensor, 0, "", ""}});
    // r1 = x; r0 = r1 * r1; r0 = r0 + r1   => x^2 + x
    Program p{{I(Op::Sense, 1, 0, 0, 0, 0, 0), I(Op::Mul, 0, 1, 1), I(Op::Add, 0, 0, 1)}};
    CHECK_NEAR(run1(s, p, {3.0}), 12.0, 1e-12);
    CHECK_NEAR(run1(s, p, {-0.5}), -0.25, 1e-12);
}

TEST(vm_protected_ops) {
    auto s = spec_with({});
    Program div0{{I(Op::Const, 1, 0, 0, 0, 5.0), I(Op::Div, 0, 1, 2)}};  // 5 / 0 -> 1
    CHECK_NEAR(run1(s, div0, {}), 1.0, 0);
    Program log0{{I(Op::Log, 0, 1)}};
    CHECK_NEAR(run1(s, log0, {}), 0.0, 0);
    Program big{{I(Op::Const, 1, 0, 0, 0, 1000.0), I(Op::Exp, 0, 1), I(Op::Mul, 0, 0, 0)}};
    CHECK(std::isfinite(run1(s, big, {})));
}

TEST(vm_if_skips_one_statement) {
    auto s = spec_with({});
    // r1 = 1; if r1 < r2 (1 < 0 false): r0 = 7 (skipped); r0 = r0 + r1
    Program p{{I(Op::Const, 1, 0, 0, 0, 1.0), I(Op::IfLt, 0, 1, 2), I(Op::Const, 0, 0, 0, 0, 7.0), I(Op::Add, 0, 0, 1)}};
    CHECK_NEAR(run1(s, p, {}), 1.0, 0);
    p.code[1] = I(Op::IfGe, 0, 1, 2);  // 1 >= 0 true
    CHECK_NEAR(run1(s, p, {}), 8.0, 0);
}

TEST(vm_loop_sums_harmonic_series) {
    auto s = spec_with({{"x", ChannelKind::Sensor, 0, "", ""}});
    // r1 = x; r2 = 1; repeat r1 times (r3 = i): { r4 = r3 + r2 ; r5 = r2 / r4 ; r0 = r0 + r5 }
    Program p{{I(Op::Sense, 1, 0, 0, 0, 0, 0), I(Op::Const, 2, 0, 0, 0, 1.0), I(Op::Loop, 3, 1, 0, 3),
               I(Op::Add, 4, 3, 2), I(Op::Div, 5, 2, 4), I(Op::Add, 0, 0, 5)}};
    CHECK_NEAR(run1(s, p, {4}), 1 + 0.5 + 1.0 / 3 + 0.25, 1e-12);
    CHECK_NEAR(run1(s, p, {0}), 0.0, 0);
}

TEST(vm_step_limit_stops_runaway_loops) {
    auto s = spec_with({});
    s.limits.max_steps = 500;
    Program p{{I(Op::Const, 1, 0, 0, 0, 1000.0), I(Op::Loop, 2, 1, 0, 1), I(Op::Loop, 3, 1, 0, 1), I(Op::Add, 0, 0, 1)}};
    RunStats st;
    run1(s, p, {}, &st);
    CHECK(st.step_limit_hit);
    CHECK(st.steps <= 500);
}

TEST(statement_structure_nests) {
    auto s = spec_with({});
    // 0 loop(body 2) { 1 if { 2 add } ; 3 add } ; 4 add
    Program p{{I(Op::Loop, 1, 1, 0, 2), I(Op::IfLt), I(Op::Add), I(Op::Add), I(Op::Add)}};
    CHECK(statement_end(s, p, 0) == 4);
    CHECK(statement_end(s, p, 1) == 3);
    CHECK(statement_end(s, p, 4) == 5);
}

TEST(effective_code_detects_introns) {
    auto s = spec_with({{"x", ChannelKind::Sensor, 0, "", ""}});
    Program p{{I(Op::Sense, 1, 0, 0, 0, 0, 0),  // effective
               I(Op::Const, 5, 0, 0, 0, 9.0),   // intron: r5 never used
               I(Op::Mul, 0, 1, 1),             // effective
               I(Op::Add, 6, 0, 0)}};           // intron
    auto m = effective_mask(s, p, 1);
    CHECK(m[0] && !m[1] && m[2] && !m[3]);
    CHECK(effective_length(s, p, 1) == 2);
}

TEST(effective_code_through_loops) {
    auto s = spec_with({{"x", ChannelKind::Sensor, 0, "", ""}});
    Program p{{I(Op::Sense, 1, 0, 0, 0, 0, 0), I(Op::Const, 2, 0, 0, 0, 1.0), I(Op::Loop, 3, 1, 0, 3),
               I(Op::Add, 4, 3, 2), I(Op::Div, 5, 2, 4), I(Op::Add, 0, 0, 5)}};
    CHECK(effective_length(s, p, 1) == 6);
}

TEST(serialization_round_trip_and_pseudogenes) {
    auto s = spec_with({{"x", ChannelKind::Sensor, 0, "", ""}, {"y", ChannelKind::Sensor, 0, "", ""}});
    Rng rng(7);
    VariationConfig v;
    v.init_min_length = 30;
    v.init_max_length = 40;
    Program p = random_program(s, v, rng);
    CHECK(program_from_json(s, program_to_json(s, p)) == p);

    // A program sensing "y", revived on a machine without "y": that instruction becomes a Nop.
    Program q{{I(Op::Sense, 0, 0, 0, 0, 0, 1)}};
    auto only_x = spec_with({{"x", ChannelKind::Sensor, 0, "", ""}});
    Program revived = program_from_json(only_x, program_to_json(s, q));
    CHECK(revived.code[0].op == Op::Nop);
}

TEST(config_json_round_trip) {
    EngineConfig a;
    a.population = 123;
    a.selection = SelectionMethod::Tournament;
    a.ops = classic_regression_ops();
    a.variation.p_crossover = 0.25;
    EngineConfig b;
    config_from_json(b, config_to_json(a));
    CHECK(b.population == 123 && b.selection == SelectionMethod::Tournament && b.ops == a.ops);
    CHECK(b.variation.p_crossover == 0.25);
}

TEST(mutation_respects_limits) {
    auto s = spec_with({{"x", ChannelKind::Sensor, 0, "", ""}});
    VariationConfig v;
    v.max_length = 16;
    Rng rng(3);
    Program p = random_program(s, v, rng);
    for (int i = 0; i < 5000; ++i) {
        mutate(p, s, v, rng);
        if (rng.chance(0.3)) p = crossover(p, random_program(s, v, rng), v, rng);
        CHECK(!p.code.empty() && static_cast<int>(p.size()) <= v.max_length);
    }
    for (const auto& in : p.code) CHECK(in.dst < s.num_registers && in.a < s.num_registers);
}

TEST(engine_is_reproducible_across_thread_counts) {
    auto problem = FormulaProblem::from_params({{"target", "koza1"}}, 5);
    EngineConfig cfg;
    cfg.seed = 5;
    cfg.population = 100;
    cfg.generations = 10;
    cfg.threads = 1;
    auto r1 = Engine(*problem, cfg).run();
    cfg.threads = 4;
    auto r2 = Engine(*problem, cfg).run();
    CHECK(r1.best == r2.best);
    CHECK(r1.best_fitness == r2.best_fitness);
}

TEST(engine_improves_on_koza1) {
    auto problem = FormulaProblem::from_params({{"target", "koza1"}}, 11);
    EngineConfig cfg;
    cfg.seed = 11;
    cfg.population = 300;
    cfg.generations = 60;
    std::vector<double> best;
    Engine e(*problem, cfg);
    e.on_generation([&](const GenerationReport& r) { best.push_back(r.best_fitness); });
    auto res = e.run();
    CHECK(best.back() > best.front());
    for (std::size_t i = 1; i < best.size(); ++i) CHECK(best[i] >= best[i - 1] - 1e-12);  // elitism
    CHECK(res.test_metrics[0].name == "r2" && res.test_metrics[0].value > 0.9);
}

TEST(registry_builds_formula_problem) {
    register_builtin_problems();
    auto p = make_problem("formula", {{"target", "nguyen10"}}, 1);
    CHECK(p->channels().size() == 2);
    CHECK(p->num_cases(Split::Train) == 100);
}

int main() {
    for (auto& [name, fn] : tests()) {
        const int before = failures;
        fn();
        std::printf("%s %s\n", failures == before ? "ok  " : "FAIL", name.c_str());
    }
    std::printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
