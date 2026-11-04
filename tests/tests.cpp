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
#include "adaptogen/problems/lists.hpp"
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

TEST(poisson_has_the_right_mean) {
    Rng r(9);
    double total = 0;
    for (int i = 0; i < 20000; ++i) total += r.poisson(2.5);
    CHECK_NEAR(total / 20000, 2.5, 0.05);
    CHECK(r.poisson(0) == 0);
}

TEST(gene_duplication_makes_a_tandem_copy) {
    auto s = spec_with({});
    VariationConfig v;
    Rng rng(4);
    for (int t = 0; t < 200; ++t) {
        Program p = random_program(s, v, rng);
        const Program before = p;
        if (!duplicate_segment(p, v, 6, rng)) continue;
        const std::size_t len = p.size() - before.size();
        CHECK(len >= 1 && len <= 6);
        // find the duplicated block: p = before[0:s+len] + before[s:s+len] + before[s+len:]
        bool found = false;
        for (std::size_t st = 0; st + len <= before.size() && !found; ++st) {
            Program expect;
            expect.code.assign(before.code.begin(), before.code.begin() + static_cast<long>(st + len));
            expect.code.insert(expect.code.end(), before.code.begin() + static_cast<long>(st), before.code.begin() + static_cast<long>(st + len));
            expect.code.insert(expect.code.end(), before.code.begin() + static_cast<long>(st + len), before.code.end());
            found = expect.code == p.code;
        }
        CHECK(found);
    }
    v.max_length = 5;
    Program small{{I(Op::Add), I(Op::Add), I(Op::Add), I(Op::Add), I(Op::Add)}};
    CHECK(!duplicate_segment(small, v, 6, rng));
}

TEST(horizontal_transfer_inserts_donor_code) {
    auto s = spec_with({});
    Program donor{{I(Op::Const, 1, 0, 0, 0, 42.0), I(Op::Mul, 2, 1, 1), I(Op::Sin, 3, 2)}};
    Program recipient{{I(Op::Add), I(Op::Sub)}};
    VariationConfig v;
    Rng rng(8);
    CHECK(transfer_segment(recipient, donor, v, 6, rng));
    CHECK(recipient.size() > 2 && recipient.size() <= 5);
    int from_donor = 0;
    for (const auto& in : recipient.code) from_donor += in.op == Op::Const || in.op == Op::Mul || in.op == Op::Sin;
    CHECK(from_donor == static_cast<int>(recipient.size()) - 2);
}

TEST(mechanisms_config_round_trip_and_validation) {
    EngineConfig a;
    a.mechanisms.stress.enabled = true;
    a.mechanisms.stress.patience = 7;
    a.mechanisms.transfer.rate = 0.33;
    a.mechanisms.spatial.enabled = a.mechanisms.curriculum.enabled = true;
    a.mechanisms.curriculum.bands = 5;
    EngineConfig b;
    config_from_json(b, config_to_json(a));
    CHECK(b.mechanisms.stress.enabled && b.mechanisms.stress.patience == 7);
    CHECK(b.mechanisms.transfer.rate == 0.33 && b.mechanisms.curriculum.bands == 5);
    EngineConfig c;
    config_from_json(c, nlohmann::json::parse(R"({"mechanisms": {"duplication": true}})"));
    CHECK(c.mechanisms.duplication.enabled);
    bool threw = false;
    try {
        EngineConfig d;
        config_from_json(d, nlohmann::json::parse(R"({"mechanisms": {"curriculum": true}})"));
    } catch (const std::exception&) {
        threw = true;
    }
    CHECK(threw);  // curriculum needs spatial
}

TEST(all_mechanisms_are_reproducible_and_bounded) {
    auto problem = FormulaProblem::from_params({{"target", "nguyen7"}}, 3);
    EngineConfig cfg;
    cfg.seed = 3;
    cfg.population = 60;
    cfg.generations = 15;
    auto& m = cfg.mechanisms;
    m.stress.enabled = m.hypermutation.enabled = m.transfer.enabled = m.duplication.enabled = true;
    m.spatial.enabled = m.curriculum.enabled = true;
    cfg.threads = 1;
    std::vector<GenerationReport> reps;
    Engine e1(*problem, cfg);
    e1.on_generation([&](const GenerationReport& r) { reps.push_back(r); });
    auto r1 = e1.run();
    cfg.threads = 4;
    auto r2 = Engine(*problem, cfg).run();
    CHECK(r1.best == r2.best);
    CHECK(r1.best.mutation_rate >= m.hypermutation.min_rate && r1.best.mutation_rate <= m.hypermutation.max_rate);
    for (std::size_t i = 1; i < reps.size(); ++i) CHECK(reps[i].best_fitness >= reps[i - 1].best_fitness);  // hall of fame
    double transfers = 0, dups = 0;
    for (const auto& r : reps)
        for (const auto& st : r.population_stats) {
            if (st.name == "transfers") transfers += st.value;
            if (st.name == "duplications") dups += st.value;
        }
    CHECK(transfers > 0 && dups > 0);
    CHECK(reps.back().program_evaluations == 64L * 15);  // 60 rounds up to an 8x8 grid
}

TEST(list_problem_accepts_a_hand_written_sum) {
    auto p = ListProblem::from_params({{"task", "sum"}}, 1);
    MachineSpec s = make_machine(*p, EngineConfig{});
    // r1 = list.length; repeat |r1| times (r2 = i): { r3 = list.item[r2]; r0 = r0 + r3 }
    Program prog{{I(Op::Sense, 1, 0, 0, 0, 0, 0), I(Op::Loop, 2, 1, 0, 2), I(Op::Sense, 3, 2, 0, 0, 0, 1),
                  I(Op::Add, 0, 0, 3)}};
    CHECK(p->solved(s, prog, Split::Train));
    CHECK(p->solved(s, prog, Split::Test));
    CHECK(effective_length(s, prog, 1) == 4);
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
