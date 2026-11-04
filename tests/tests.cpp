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
#include "adaptogen/plugin.hpp"
#include "adaptogen/simplify.hpp"
#include "adaptogen/strain.hpp"

#include <filesystem>

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
    cfg.downsample = 1.0;  // every case each generation
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

TEST(validation_split_guards_the_final_choice) {
    auto problem = ListProblem::from_params({{"task", "sum"}}, 2);
    EngineConfig cfg;
    cfg.seed = 2;
    cfg.population = 80;
    cfg.generations = 8;
    cfg.validation_fraction = 0.25;
    std::vector<GenerationReport> reps;
    Engine e(*problem, cfg);
    e.on_generation([&](const GenerationReport& r) { reps.push_back(r); });
    e.run();
    double last = -1e300;
    for (const auto& r : reps) {
        bool found = false;
        for (const auto& st : r.population_stats)
            if (st.name == "best_validation_score") {
                found = true;
                CHECK(st.value >= last - 1e-12);  // the champion's validation score never drops
                last = st.value;
            }
        CHECK(found);
    }
    EngineConfig b;
    config_from_json(b, config_to_json(cfg));
    CHECK(b.validation_fraction == 0.25);
}

TEST(population_stress_trigger) {
    EngineConfig cfg;
    config_from_json(cfg, nlohmann::json::parse(R"({"mechanisms": {"stress": {"enabled": true, "trigger": "population", "patience": 2}}})"));
    CHECK(cfg.mechanisms.stress.trigger == "population");
    bool threw = false;
    try {
        EngineConfig d;
        config_from_json(d, nlohmann::json::parse(R"({"mechanisms": {"stress": {"trigger": "sometimes"}}})"));
    } catch (const std::exception&) {
        threw = true;
    }
    CHECK(threw);
    // A tiny population on an unsolvable budget stagnates; the population-wide multiplier must kick in.
    auto problem = FormulaProblem::from_params({{"target", "pagie1"}}, 1);
    cfg.population = 20;
    cfg.generations = 30;
    cfg.seed = 1;
    double max_stress = 0;
    Engine e(*problem, cfg);
    e.on_generation([&](const GenerationReport& r) {
        for (const auto& st : r.population_stats)
            if (st.name == "mean_stress_multiplier") max_stress = std::max(max_stress, st.value);
    });
    e.run();
    CHECK(max_stress > 1.0);
}

namespace {
// A hand-written strain that sums a list, built the same way the CLI builds one.
Strain sum_strain(const ListProblem& problem, EngineConfig cfg) {
    MachineSpec s = make_machine(problem, cfg);
    EvolutionResult r;
    r.best = Program{{I(Op::Sense, 1, 0, 0, 0, 0, 0), I(Op::Loop, 2, 1, 0, 2), I(Op::Sense, 3, 2, 0, 0, 0, 1),
                      I(Op::Add, 0, 0, 3), I(Op::Const, 6, 0, 0, 0, 3.0)}};  // last one is an intron
    r.test_metrics = problem.metrics(s, r.best, Split::Test);
    r.solved_test = problem.solved(s, r.best, Split::Test);
    return make_strain("list-sum", "sums a list", problem, s, r,
                       {{"name", "list"}, {"params", {{"task", "sum"}}}, {"seed", 1}}, cfg, {});
}

std::filesystem::path temp_library(const char* name) {
    auto dir = std::filesystem::temp_directory_path() / (std::string("adaptogen-test-") + name);
    std::filesystem::remove_all(dir);
    return dir;
}
}  // namespace

TEST(strain_save_load_and_versioning) {
    auto problem = ListProblem::from_params({{"task", "sum"}}, 1);
    StrainLibrary lib(temp_library("lib1"));
    Strain s = sum_strain(*problem, EngineConfig{});
    CHECK(s.channel_used.size() == 2 && s.channel_used[0] && s.channel_used[1]);
    const std::string id1 = lib.save(s);
    const std::string id2 = lib.save(s);
    CHECK(id1 == "list-sum-v1" && id2 == "list-sum-v2");
    CHECK(lib.list().size() == 2);
    CHECK(lib.load("list-sum").version == 2);   // a name loads the latest version
    CHECK(lib.load("list-sum-v1").version == 1);
    const Strain back = lib.load(id1);
    CHECK(back.program == s.program && back.channels.size() == 2 && back.solved_test);
    CHECK(back.problem.at("name") == "list");
    std::filesystem::remove(lib.dir() / "index.json");
    CHECK(lib.list().size() == 2);  // index is rebuilt from the strain files
    CHECK(!StrainLibrary::valid_name("Bad Name") && !StrainLibrary::valid_name("sum-v3") && StrainLibrary::valid_name("sum.max_2"));
    bool threw = false;
    try {
        lib.load("does-not-exist");
    } catch (const std::exception&) {
        threw = true;
    }
    CHECK(threw);
}

TEST(strain_revives_on_compatible_problem) {
    auto sum = ListProblem::from_params({{"task", "sum"}}, 1);
    auto max = ListProblem::from_params({{"task", "max"}}, 2);
    Strain s = sum_strain(*sum, EngineConfig{});
    const MachineSpec spec = make_machine(*max, EngineConfig{});
    const Compatibility c = check_compatibility(s, spec.channels);
    CHECK(c.missing.empty() && c.added.empty() && c.coverage() == 1.0);
    const Program p = revive(s, spec);
    CHECK(p.size() == 5 && effective_length(spec, p, 1) == 4);  // the loop structure carries over intact
}

TEST(strain_pseudogenes_and_channel_maps) {
    auto sum = ListProblem::from_params({{"task", "sum"}}, 1);
    Strain s = sum_strain(*sum, EngineConfig{});
    // A formula problem has none of the list channels: the strain's sensing becomes pseudogenes.
    auto formula = FormulaProblem::from_params({{"target", "koza1"}}, 1);
    const MachineSpec fspec = make_machine(*formula, EngineConfig{});
    const Compatibility c = check_compatibility(s, fspec.channels);
    CHECK(c.matched.empty() && c.missing.size() == 2 && c.used_missing == 2 && c.coverage() == 0.0);
    const Program p = revive(s, fspec);
    int nops = 0;
    for (const auto& in : p.code) nops += in.op == Op::Nop;
    CHECK(nops == 2);
    // A mapping renames channels; a different arity counts as missing; different units warn.
    std::vector<Channel> renamed = {{"items.count", ChannelKind::Sensor, 0, "number of items in the list", "items"},
                                    {"items.at", ChannelKind::Sensor, 2, "", ""}};
    const Compatibility m = check_compatibility(s, renamed, {{"list.length", "items.count"}, {"list.item", "items.at"}});
    CHECK(m.matched.size() == 1 && m.missing.size() == 1 && !m.warnings.empty());
    std::vector<Channel> units = {{"list.length", ChannelKind::Sensor, 0, "number of items in the list", "metres"}};
    bool warned = false;
    for (const auto& w : check_compatibility(s, units).warnings) warned = warned || w.find("units differ") != std::string::npos;
    CHECK(warned);
}

TEST(seeded_run_starts_from_the_strain) {
    auto dir = temp_library("lib2");
    StrainLibrary lib(dir);
    auto sum = ListProblem::from_params({{"task", "sum"}}, 1);
    lib.save(sum_strain(*sum, EngineConfig{}));
    // Seeded with a perfect sum strain, a sum problem is solved in the first generation.
    auto target = ListProblem::from_params({{"task", "sum"}}, 9);
    EngineConfig cfg;
    cfg.population = 50;
    cfg.generations = 5;
    cfg.seeding.strains = {"list-sum"};
    cfg.seeding.library = dir.string();
    PreparedRun prep;
    const auto res = run_evolution(*target, cfg, {}, &prep);
    CHECK(prep.strains.size() == 1 && prep.strains[0].id == "list-sum-v1");
    CHECK(res.generations == 1 && res.solved_test);
}

TEST(retention_scores_parent_problems) {
    auto dir = temp_library("lib3");
    StrainLibrary lib(dir);
    auto sum = ListProblem::from_params({{"task", "sum"}}, 1);
    lib.save(sum_strain(*sum, EngineConfig{}));
    register_builtin_problems();
    auto max = ListProblem::from_params({{"task", "max"}}, 2);
    EngineConfig cfg;
    cfg.seeding.strains = {"list-sum"};
    cfg.seeding.library = dir.string();
    cfg.seeding.retain_weight = 0.3;
    PreparedRun prep = prepare_seeding(*max, cfg);
    const Problem& p = prep.problem(*max);
    CHECK(prep.retention != nullptr);
    CHECK(p.num_cases(Split::Train) == max->num_cases(Split::Train) + sum->num_cases(Split::Train));
    CHECK(p.num_cases(Split::Test) == max->num_cases(Split::Test));
    const MachineSpec spec = make_machine(p, cfg);
    // The sum program is perfect on the parent (sum) cases: its retained score is full accuracy.
    const auto ms = p.metrics(spec, prep.seeds[0], Split::Train);
    bool found = false;
    for (const auto& m : ms)
        if (m.name.rfind("retained:", 0) == 0) {
            found = true;
            CHECK(m.value == 1.0);
        }
    CHECK(found);
    // Mean over all training cases = 0.7 * primary mean + 0.3 * parent mean.
    const double mixed = p.mean_score(spec, prep.seeds[0], Split::Train);
    const double primary = max->mean_score(spec, prep.seeds[0], Split::Train);
    CHECK_NEAR(mixed, 0.7 * primary + 0.3 * 0.0, 1e-9);
}

TEST(simplifier_reduces_to_the_essential_loop) {
    auto sum = ListProblem::from_params({{"task", "sum"}}, 1);
    const MachineSpec s = make_machine(*sum, EngineConfig{});
    // A sum loop buried in junk: dead constants, a no-op if, a redundant copy.
    Program bloated{{I(Op::Const, 5, 0, 0, 0, 2.0), I(Op::Sense, 1, 0, 0, 0, 0, 0), I(Op::IfLt, 0, 5, 6),
                     I(Op::Mul, 7, 5, 5), I(Op::Loop, 2, 1, 0, 3), I(Op::Mov, 6, 2), I(Op::Sense, 3, 2, 0, 0, 0, 1),
                     I(Op::Add, 0, 0, 3), I(Op::Sin, 4, 4)}};
    CHECK(sum->solved(s, bloated, Split::Train));
    const auto r = simplify(*sum, s, bloated);
    CHECK(r.length_before == 9);
    CHECK(r.length_after == 4);  // length, loop, item, add
    CHECK(sum->solved(s, r.program, Split::Train) && sum->solved(s, r.program, Split::Test));
}

TEST(simplifier_never_makes_any_case_worse) {
    auto problem = FormulaProblem::from_params({{"target", "nguyen7"}}, 4);
    const MachineSpec s = make_machine(*problem, EngineConfig{});
    VariationConfig v;
    v.init_min_length = 20;
    v.init_max_length = 60;
    Rng rng(12);
    int shrunk = 0;
    for (int t = 0; t < 40; ++t) {
        const Program p = random_program(s, v, rng);
        const auto r = simplify(*problem, s, p);
        CHECK(r.length_after <= r.length_before);
        shrunk += r.length_after < r.length_before;
        Runner a(s), b(s);
        for (std::size_t c = 0; c < problem->num_cases(Split::Train); ++c)
            CHECK(problem->evaluate_case(b, r.program, Split::Train, c) >=
                  problem->evaluate_case(a, p, Split::Train, c) - 1e-9);
    }
    CHECK(shrunk > 30);
}

TEST(downsampling_minimum_sample_and_curriculum) {
    // 6 training cases at 10% would be a single case: the minimum keeps 3.
    Dataset d;
    d.variables = {"x"};
    for (int i = 0; i < 6; ++i) {
        d.x.push_back({static_cast<double>(i)});
        d.y.push_back(i * 2.0);
    }
    FormulaProblem tiny("tiny", "", d, d);
    EngineConfig cfg;
    cfg.population = 10;
    cfg.generations = 4;
    cfg.stop_when_solved = false;
    const auto r = Engine(tiny, cfg).run();
    CHECK(r.case_evaluations == 4L * (10 * 3 + 6));
    // The curriculum always scores every case (down-sampling is ignored rather than an error).
    cfg.mechanisms.spatial.enabled = cfg.mechanisms.curriculum.enabled = true;
    cfg.population = 9;
    const auto c = Engine(tiny, cfg).run();
    CHECK(c.case_evaluations == 4L * 9 * 6);
}

TEST(downsampling_and_case_budget) {
    auto problem = ListProblem::from_params({{"task", "sum"}}, 3);  // 100 training cases
    EngineConfig cfg;
    cfg.seed = 3;
    cfg.population = 40;
    cfg.generations = 10;
    cfg.stop_when_solved = false;
    cfg.downsample = 0.25;
    std::vector<GenerationReport> reps;
    Engine e(*problem, cfg);
    e.on_generation([&](const GenerationReport& r) { reps.push_back(r); });
    const auto res = e.run();
    // Each generation: 40 programs x 25 cases, plus the champion re-judged on all 100 cases.
    CHECK(res.case_evaluations == 10L * (40 * 25 + 100));
    for (std::size_t i = 1; i < reps.size(); ++i) CHECK(reps[i].best_fitness >= reps[i - 1].best_fitness);
    // A case budget stops the run before the generation limit.
    cfg.generations = 1000;
    cfg.max_case_evaluations = 20000;
    const auto b = Engine(*problem, cfg).run();
    CHECK(b.case_evaluations <= 20000 && b.generations < 1000);
    EngineConfig back;
    config_from_json(back, config_to_json(cfg));
    CHECK(back.downsample == 0.25 && back.max_case_evaluations == 20000);
    bool threw = false;
    try {
        EngineConfig bad;
        config_from_json(bad, nlohmann::json::parse(R"({"downsample": 0})"));
    } catch (const std::exception&) {
        threw = true;
    }
    CHECK(threw);
}

TEST(duplicate_cache_changes_nothing_but_cost) {
    auto problem = FormulaProblem::from_params({{"target", "nguyen7"}}, 6);
    EngineConfig cfg;
    cfg.seed = 6;
    cfg.population = 80;
    cfg.generations = 15;
    cfg.stop_when_solved = false;
    cfg.downsample = 1.0;  // the cache only applies when scores stay valid across generations
    const auto plain = Engine(*problem, cfg).run();
    cfg.cache_duplicates = true;
    const auto cached = Engine(*problem, cfg).run();
    CHECK(plain.best == cached.best && plain.best_fitness == cached.best_fitness);
    CHECK(cached.case_evaluations < plain.case_evaluations);
}

TEST(constant_tuning_improves_constants) {
    // Target is a constant; the only way to improve a program "r0 = c" is to tune c.
    Dataset d;
    d.variables = {"x"};
    for (int i = 0; i < 10; ++i) {
        d.x.push_back({static_cast<double>(i)});
        d.y.push_back(2.718);
    }
    FormulaProblem problem("const", "constant target", d, d);
    EngineConfig cfg;
    cfg.seed = 2;
    cfg.population = 2;
    cfg.generations = 30;
    cfg.elitism = 2;  // keep the program unchanged by variation: only tuning can improve it
    cfg.ops = {Op::Const};
    cfg.num_registers = 1;  // the constant always lands in the output register
    cfg.variation.init_min_length = cfg.variation.init_max_length = 1;
    cfg.variation.sense_weight = 0;
    cfg.constant_tuning.enabled = true;
    cfg.constant_tuning.steps = 30;
    const auto res = Engine(problem, cfg).run();
    CHECK(res.best.code.size() == 1 && res.best.code[0].op == Op::Const);
    CHECK(std::fabs(res.best.code[0].imm - 2.718) < 0.05);
}

namespace {
std::string fixture(const char* name) { return std::string(ADAPTOGEN_TEST_DATA_DIR) + "/plugins/" + name; }
Program constant_program(double v) { return Program{{I(Op::Const, 0, 0, 0, 0, v)}}; }
}  // namespace

TEST(plugin_crashes_and_hangs_are_contained) {
    auto p = make_plugin_problem(fixture("misbehaving.cpp"), nlohmann::json::object(), 1);
    p->timeout_seconds = 2;
    const MachineSpec spec = make_machine(*p, EngineConfig{});
    // good, crasher, hanger, NaN, good: only the bad ones are penalised.
    const Program good = constant_program(4), crash = constant_program(5000), hang = constant_program(-5000),
                  nan = constant_program(777);
    std::vector<const Program*> batch = {&good, &crash, &hang, &nan, &good};
    std::vector<std::size_t> cases = {2, 3};
    std::vector<double> scores;
    std::vector<long> steps;
    p->evaluate_batch(spec, batch, Split::Train, cases, 2, scores, steps);
    CHECK(scores.size() == 10);
    CHECK(scores[0] == 0.0 && scores[1] == -2.0);  // r0 = 4 vs 2x = 4 and 6
    CHECK(scores[2] == PluginProblem::kFailureScore && scores[3] == PluginProblem::kFailureScore);  // crash
    CHECK(scores[4] == PluginProblem::kFailureScore && scores[5] == PluginProblem::kFailureScore);  // hang
    CHECK(scores[6] == PluginProblem::kFailureScore);                                               // NaN
    CHECK(scores[8] == 0.0 && scores[9] == -2.0);
    CHECK(p->failures() == 2);
    // The problem keeps working after the failures (workers were restarted).
    std::vector<const Program*> again = {&good};
    p->evaluate_batch(spec, again, Split::Test, {4}, 1, scores, steps);
    CHECK(scores.size() == 1 && scores[0] == -4.0);
    // Evolution on it runs to completion despite crashing programs.
    EngineConfig cfg;
    cfg.population = 30;
    cfg.generations = 5;
    cfg.const_min = -6000;
    cfg.const_max = 6000;
    const auto res = Engine(*p, cfg).run();
    CHECK(res.generations >= 1);
}

TEST(plugin_build_reports_compile_errors) {
    const BuildResult b = build_plugin(fixture("broken.cpp"));
    CHECK(!b.ok);
    bool found = false;
    for (const auto& d : b.diagnostics) found = found || (d.severity == "error" && d.line == 9);
    CHECK(found);
    bool threw = false;
    try {
        make_plugin_problem(fixture("broken.cpp"), nlohmann::json::object(), 1);
    } catch (const std::exception& e) {
        threw = std::string(e.what()).find("does not compile") != std::string::npos;
    }
    CHECK(threw);
}

TEST(plugin_validation_checks_inheritance) {
    const auto lib = std::filesystem::path(ADAPTOGEN_TEST_DATA_DIR).parent_path().parent_path() / "benchmarks" / "strains";
    const ValidationReport bad = validate_plugin(fixture("redefines.cpp"), nlohmann::json::object(), 1, lib);
    CHECK(!bad.ok);
    bool redefined = false;
    for (const auto& e : bad.errors) redefined = redefined || e.find("redefined") != std::string::npos;
    CHECK(redefined);
    const ValidationReport nan = validate_plugin(fixture("misbehaving.cpp"), nlohmann::json::object(), 1, lib);
    CHECK(nan.info.value("name", "") == "misbehaving");
}

TEST(base_header_merges_and_detects_conflicts) {
    auto sum = ListProblem::from_params({{"task", "sum"}}, 1);
    Strain a = sum_strain(*sum, EngineConfig{});
    a.id = "a-v1";
    a.name = "a";
    Strain b = a;  // same interface: merged (diamond)
    b.id = "b-v1";
    b.name = "b";
    const std::string h = generate_base_header({a, b}, "MergedBase");
    CHECK(h.find("class MergedBase") != std::string::npos);
    CHECK(h.find("kInheritedChannels = 2") != std::string::npos);
    CHECK(h.find("\"a-v1\", \"b-v1\"") != std::string::npos);
    Strain c = a;  // same name, different units: conflict
    c.id = "c-v1";
    c.name = "c";
    c.channels[0].units = "metres";
    bool threw = false;
    try {
        generate_base_header({a, c}, "Bad");
    } catch (const std::exception& e) {
        threw = std::string(e.what()).find("differs") != std::string::npos;
    }
    CHECK(threw);
    // A rename resolves it, and is recorded as a channel map for seeding.
    const std::string r = generate_base_header({a, c}, "Renamed", {{"c-v1:list.length", "c.length"}});
    CHECK(r.find("kInheritedChannels = 3") != std::string::npos);
    CHECK(r.find("{\"c-v1:list.length\", \"c.length\"}") != std::string::npos);
}

TEST(seeding_config_round_trip) {
    EngineConfig a;
    a.seeding.strains = {"x-v1", "y"};
    a.seeding.retain_weight = 0.2;
    a.seeding.channel_map = {{"a", "b"}};
    EngineConfig b;
    config_from_json(b, config_to_json(a));
    CHECK(b.seeding.strains == a.seeding.strains && b.seeding.retain_weight == 0.2);
    CHECK(b.seeding.channel_map.size() == 1 && b.seeding.channel_map[0].second == "b");
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
