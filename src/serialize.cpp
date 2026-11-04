#include "adaptogen/serialize.hpp"

#include <algorithm>
#include <functional>
#include <stdexcept>

#include "adaptogen/analysis.hpp"

namespace adaptogen {

using nlohmann::json;

json program_to_json(const MachineSpec& spec, const Program& p) {
    json code = json::array();
    for (const Instr& in : p.code) {
        json j;
        j["op"] = std::string(op_info(in.op).name);
        switch (in.op) {
            case Op::Nop: break;
            case Op::Const: j["dst"] = in.dst; j["imm"] = in.imm; break;
            case Op::IfLt: case Op::IfGe: case Op::IfEq: j["a"] = in.a; j["b"] = in.b; break;
            case Op::Loop: j["dst"] = in.dst; j["a"] = in.a; j["body"] = in.c; break;
            case Op::Sense:
            case Op::Act: {
                j["dst"] = in.dst;
                j["channel"] = in.channel < spec.channels.size() ? spec.channels[in.channel].name : "";
                j["args"] = json::array({in.a, in.b, in.c});
                break;
            }
            default:
                j["dst"] = in.dst;
                j["a"] = in.a;
                if (op_info(in.op).sources >= 2) j["b"] = in.b;
        }
        code.push_back(std::move(j));
    }
    return json{{"format", "adaptogen-program/1"},
                {"registers", spec.num_registers},
                {"mutation_rate", p.mutation_rate},
                {"code", code}};
}

Program program_from_json(const MachineSpec& spec, const json& j) {
    Program p;
    const int R = spec.num_registers;
    auto reg = [&](const json& x, const char* key) -> std::uint8_t {
        if (!x.contains(key)) return 0;
        int v = x.at(key).get<int>() % R;
        return static_cast<std::uint8_t>(v < 0 ? v + R : v);
    };
    p.mutation_rate = j.value("mutation_rate", 1.0);
    for (const json& x : j.at("code")) {
        Instr in;
        const std::string name = x.at("op").get<std::string>();
        auto op = op_from_name(name);
        if (!op) throw std::runtime_error("unknown op '" + name + "'");
        in.op = *op;
        in.dst = reg(x, "dst");
        in.a = reg(x, "a");
        in.b = reg(x, "b");
        if (x.contains("imm")) in.imm = x.at("imm").get<double>();
        if (in.op == Op::Loop) in.c = static_cast<std::uint8_t>(std::max(1, x.value("body", 1)));
        if (in.op == Op::Sense || in.op == Op::Act) {
            const std::string ch = x.value("channel", "");
            const auto want = in.op == Op::Sense ? ChannelKind::Sensor : ChannelKind::Action;
            bool found = false;
            for (std::size_t k = 0; k < spec.channels.size(); ++k)
                if (spec.channels[k].name == ch && spec.channels[k].kind == want) {
                    in.channel = static_cast<std::uint16_t>(k);
                    found = true;
                }
            if (x.contains("args")) {
                const auto& args = x.at("args");
                std::uint8_t* f[3] = {&in.a, &in.b, &in.c};
                for (std::size_t k = 0; k < 3 && k < args.size(); ++k) {
                    int v = args[k].get<int>() % R;
                    *f[k] = static_cast<std::uint8_t>(v < 0 ? v + R : v);
                }
            }
            if (!found) in = Instr{};  // pseudogene: the channel does not exist here
        }
        canonicalize(in);
        p.code.push_back(in);
    }
    return p;
}

namespace {

// ------------------------------------------------------------------ config fields
//
// Every config section is a table of fields. Each field knows its JSON name, how to read it and
// how to write it, so reading and writing cannot drift apart and unknown keys are reported
// uniformly ("unknown key 'x' in variation").

template <class T>
struct Field {
    const char* name;
    std::function<void(T&, const json&)> read;
    std::function<json(const T&)> write;
};
template <class T>
using Fields = std::vector<Field<T>>;

/// A plain member: read with get<M>(), written as-is.
template <class T, class M>
Field<T> field(const char* name, M T::*member) {
    return {name, [member](T& t, const json& j) { t.*member = j.get<M>(); }, [member](const T& t) { return json(t.*member); }};
}

/// A member of a member (e.g. EngineConfig::limits.max_steps), flattened into this section.
template <class T, class S, class M>
Field<T> field(const char* name, S T::*outer, M S::*inner) {
    return {name, [outer, inner](T& t, const json& j) { (t.*outer).*inner = j.get<M>(); },
            [outer, inner](const T& t) { return json((t.*outer).*inner); }};
}

template <class T>
void read_section(T& obj, const json& j, const Fields<T>& fields, const std::string& section) {
    if (!j.is_object()) throw std::runtime_error(section + " must be a JSON object");
    for (const auto& [key, value] : j.items()) {
        const auto it = std::find_if(fields.begin(), fields.end(), [&](const Field<T>& f) { return key == f.name; });
        if (it == fields.end()) throw std::runtime_error("unknown key '" + key + "' in " + section);
        it->read(obj, value);
    }
}

template <class T>
json write_section(const T& obj, const Fields<T>& fields) {
    json j = json::object();
    for (const auto& f : fields) j[f.name] = f.write(obj);
    return j;
}

/// A nested section; `true`/`false` is shorthand for {"enabled": ...} when the section has one.
template <class T, class S>
Field<T> section(const char* name, S T::*member, const Fields<S>& (*fields)(), std::string path,
                 void (*validate)(const S&) = nullptr) {
    return {name,
            [=](T& t, const json& j) {
                S& s = t.*member;
                if (j.is_boolean()) read_section(s, json{{"enabled", j.get<bool>()}}, fields(), path);
                else read_section(s, j, fields(), path);
                if (validate) validate(s);
            },
            [=](const T& t) { return write_section(t.*member, fields()); }};
}

const Fields<Mechanisms::Stress>& stress_fields() {
    using S = Mechanisms::Stress;
    static const Fields<S> f = {field("enabled", &S::enabled), field("trigger", &S::trigger), field("patience", &S::patience),
                                field("growth", &S::growth), field("max_multiplier", &S::max_multiplier)};
    return f;
}
const Fields<Mechanisms::Hypermutation>& hypermutation_fields() {
    using H = Mechanisms::Hypermutation;
    static const Fields<H> f = {field("enabled", &H::enabled),   field("initial_rate", &H::initial_rate),
                                field("min_rate", &H::min_rate), field("max_rate", &H::max_rate),
                                field("p_change", &H::p_change), field("change_sigma", &H::change_sigma)};
    return f;
}
const Fields<Mechanisms::Transfer>& transfer_fields() {
    using X = Mechanisms::Transfer;
    static const Fields<X> f = {field("enabled", &X::enabled), field("rate", &X::rate), field("max_segment", &X::max_segment)};
    return f;
}
const Fields<Mechanisms::Duplication>& duplication_fields() {
    using D = Mechanisms::Duplication;
    static const Fields<D> f = {field("enabled", &D::enabled), field("rate", &D::rate), field("max_segment", &D::max_segment)};
    return f;
}
const Fields<Mechanisms::Spatial>& spatial_fields() {
    using P = Mechanisms::Spatial;
    static const Fields<P> f = {field("enabled", &P::enabled), field("width", &P::width), field("radius", &P::radius)};
    return f;
}
const Fields<Mechanisms::Curriculum>& curriculum_fields() {
    using C = Mechanisms::Curriculum;
    static const Fields<C> f = {field("enabled", &C::enabled), field("bands", &C::bands), field("refresh", &C::refresh)};
    return f;
}
void validate_stress(const Mechanisms::Stress& s) {
    if (s.trigger != "lineage" && s.trigger != "population")
        throw std::runtime_error("mechanisms.stress.trigger must be \"lineage\" or \"population\"");
}
const Fields<Mechanisms>& mechanism_fields() {
    using M = Mechanisms;
    static const Fields<M> f = {
        section("stress", &M::stress, stress_fields, "mechanisms.stress", validate_stress),
        section("hypermutation", &M::hypermutation, hypermutation_fields, "mechanisms.hypermutation"),
        section("transfer", &M::transfer, transfer_fields, "mechanisms.transfer"),
        section("duplication", &M::duplication, duplication_fields, "mechanisms.duplication"),
        section("spatial", &M::spatial, spatial_fields, "mechanisms.spatial"),
        section("curriculum", &M::curriculum, curriculum_fields, "mechanisms.curriculum"),
    };
    return f;
}
void validate_mechanisms(const Mechanisms& m) {
    if (m.curriculum.enabled && !m.spatial.enabled)
        throw std::runtime_error("mechanisms.curriculum requires mechanisms.spatial to be enabled");
}

const Fields<EngineConfig::ConstantTuning>& constant_tuning_fields() {
    using C = EngineConfig::ConstantTuning;
    static const Fields<C> f = {field("enabled", &C::enabled), field("top", &C::top), field("steps", &C::steps), field("sigma", &C::sigma)};
    return f;
}

const Fields<EngineConfig::Seeding>& seeding_fields() {
    using S = EngineConfig::Seeding;
    static const Fields<S> f = {
        field("strains", &S::strains),
        field("library", &S::library),
        field("random_fraction", &S::random_fraction),
        field("retain_weight", &S::retain_weight),
        {"channel_map",
         [](S& s, const json& j) {
             s.channel_map.clear();
             for (const auto& [from, to] : j.items()) s.channel_map.emplace_back(from, to.get<std::string>());
         },
         [](const S& s) {
             json map = json::object();
             for (const auto& [from, to] : s.channel_map) map[from] = to;
             return map;
         }},
    };
    return f;
}
void validate_seeding(const EngineConfig::Seeding& s) {
    if (s.retain_weight < 0 || s.retain_weight >= 1) throw std::runtime_error("seeding.retain_weight must be in [0, 1)");
}

const Fields<VariationConfig>& variation_fields() {
    using V = VariationConfig;
    static const Fields<V> f = {
        field("init_min_length", &V::init_min_length), field("init_max_length", &V::init_max_length),
        field("max_length", &V::max_length),           field("sense_weight", &V::sense_weight),
        field("act_weight", &V::act_weight),           field("p_crossover", &V::p_crossover),
        field("p_mutation", &V::p_mutation),           field("w_insert", &V::w_insert),
        field("w_delete", &V::w_delete),               field("w_point", &V::w_point),
        field("w_constant", &V::w_constant),           field("constant_sigma", &V::constant_sigma),
        field("effective_mutation", &V::effective_mutation),
    };
    return f;
}

std::vector<Op> ops_from_json(const json& j) {
    if (j.is_string()) {
        const auto s = j.get<std::string>();
        if (s == "core") return core_ops();
        if (s == "classic") return classic_regression_ops();
        throw std::runtime_error("ops must be \"core\", \"classic\" or a list of op names");
    }
    std::vector<Op> ops;
    for (const auto& x : j) {
        auto op = op_from_name(x.get<std::string>());
        if (!op || *op == Op::Sense || *op == Op::Act || *op == Op::Nop)
            throw std::runtime_error("invalid op in config: " + x.get<std::string>());
        ops.push_back(*op);
    }
    return ops;
}

const Fields<EngineConfig>& config_fields() {
    using E = EngineConfig;
    static const Fields<E> f = {
        field("seed", &E::seed),
        field("population", &E::population),
        field("generations", &E::generations),
        field("elitism", &E::elitism),
        {"selection",
         [](E& c, const json& j) {
             const auto s = j.get<std::string>();
             if (s == "tournament") c.selection = SelectionMethod::Tournament;
             else if (s == "lexicase") c.selection = SelectionMethod::Lexicase;
             else throw std::runtime_error("selection must be tournament or lexicase");
         },
         [](const E& c) { return json(to_string(c.selection)); }},
        field("tournament_size", &E::tournament_size),
        field("registers", &E::num_registers),
        {"ops", [](E& c, const json& j) { c.ops = ops_from_json(j); },
         [](const E& c) {
             json ops = json::array();
             for (Op op : c.ops) ops.push_back(std::string(op_info(op).name));
             return ops;
         }},
        field("max_steps", &E::limits, &VmLimits::max_steps),
        field("max_loop_iterations", &E::limits, &VmLimits::max_loop_iterations),
        field("max_loop_body", &E::limits, &VmLimits::max_loop_body),
        field("const_min", &E::const_min),
        field("const_max", &E::const_max),
        field("length_cost", &E::length_cost),
        field("time_cost", &E::time_cost),
        field("threads", &E::threads),
        field("stop_when_solved", &E::stop_when_solved),
        field("max_program_evaluations", &E::max_program_evaluations),
        field("validation_fraction", &E::validation_fraction),
        field("max_case_evaluations", &E::max_case_evaluations),
        {"downsample",
         [](E& c, const json& j) {
             c.downsample = j.get<double>();
             if (!(c.downsample > 0 && c.downsample <= 1)) throw std::runtime_error("downsample must be in (0, 1]");
         },
         [](const E& c) { return json(c.downsample); }},
        field("downsample_min_cases", &E::downsample_min_cases),
        field("cache_duplicates", &E::cache_duplicates),
        section("constant_tuning", &E::constant_tuning, constant_tuning_fields, "constant_tuning"),
        section("seeding", &E::seeding, seeding_fields, "seeding", validate_seeding),
        section("mechanisms", &E::mechanisms, mechanism_fields, "mechanisms", validate_mechanisms),
        section("variation", &E::variation, variation_fields, "variation"),
    };
    return f;
}

}  // namespace

json config_to_json(const EngineConfig& c) { return write_section(c, config_fields()); }

void config_from_json(EngineConfig& c, const json& j) { read_section(c, j, config_fields(), "config"); }

json metrics_to_json(const std::vector<Metric>& ms) {
    json j = json::object();
    for (const auto& m : ms) j[m.name] = m.value;
    return j;
}

json report_to_json(const GenerationReport& r) {
    return json{{"event", "generation"},
                {"generation", r.generation},
                {"program_evaluations", r.program_evaluations},
                {"case_evaluations", r.case_evaluations},
                {"seconds", r.seconds},
                {"best_fitness", r.best_fitness},
                {"mean_fitness", r.mean_fitness},
                {"best_length", r.best_length},
                {"best_effective_length", r.best_effective_length},
                {"mean_length", r.mean_length},
                {"best_train_metrics", metrics_to_json(r.best_train_metrics)},
                {"population_stats", metrics_to_json(r.population_stats)}};
}

json result_to_json(const MachineSpec& spec, const EvolutionResult& r) {
    return json{{"event", "result"},
                {"generations", r.generations},
                {"program_evaluations", r.program_evaluations},
                {"case_evaluations", r.case_evaluations},
                {"seconds", r.seconds},
                {"best_fitness", r.best_fitness},
                {"solved_train", r.solved_train},
                {"solved_test", r.solved_test},
                {"train_metrics", metrics_to_json(r.train_metrics)},
                {"test_metrics", metrics_to_json(r.test_metrics)},
                {"program", program_to_json(spec, r.best)}};
}

}  // namespace adaptogen
