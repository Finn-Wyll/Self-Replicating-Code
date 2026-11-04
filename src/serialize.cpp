#include "adaptogen/serialize.hpp"

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

json mechanisms_to_json(const Mechanisms& m) {
    return json{
        {"stress", {{"enabled", m.stress.enabled}, {"trigger", m.stress.trigger}, {"patience", m.stress.patience}, {"growth", m.stress.growth},
                    {"max_multiplier", m.stress.max_multiplier}}},
        {"hypermutation", {{"enabled", m.hypermutation.enabled}, {"initial_rate", m.hypermutation.initial_rate},
                           {"min_rate", m.hypermutation.min_rate}, {"max_rate", m.hypermutation.max_rate},
                           {"p_change", m.hypermutation.p_change}, {"change_sigma", m.hypermutation.change_sigma}}},
        {"transfer", {{"enabled", m.transfer.enabled}, {"rate", m.transfer.rate}, {"max_segment", m.transfer.max_segment}}},
        {"duplication", {{"enabled", m.duplication.enabled}, {"rate", m.duplication.rate},
                         {"max_segment", m.duplication.max_segment}}},
        {"spatial", {{"enabled", m.spatial.enabled}, {"width", m.spatial.width}, {"radius", m.spatial.radius}}},
        {"curriculum", {{"enabled", m.curriculum.enabled}, {"bands", m.curriculum.bands}, {"refresh", m.curriculum.refresh}}},
    };
}

template <class T>
void set_field(const json& section, const char* name, T& field) {
    if (section.contains(name)) field = section.at(name).get<T>();
}

void check_keys(const std::string& section, const json& j, std::initializer_list<const char*> allowed) {
    for (const auto& [k, v] : j.items()) {
        (void)v;
        bool ok = false;
        for (const char* a : allowed) ok = ok || k == a;
        if (!ok) throw std::runtime_error("unknown key '" + k + "' in " + section);
    }
}

void mechanisms_from_json(Mechanisms& m, const json& j) {
    for (const auto& [name, s] : j.items()) {
        if (s.is_boolean()) {  // shorthand: "stress": true
            json e = {{"enabled", s.get<bool>()}};
            mechanisms_from_json(m, json{{name, e}});
            continue;
        }
        if (name == "stress") {
            check_keys("mechanisms." + name, s, {"enabled", "trigger", "patience", "growth", "max_multiplier"});
            set_field(s, "enabled", m.stress.enabled);
            set_field(s, "trigger", m.stress.trigger);
            if (m.stress.trigger != "lineage" && m.stress.trigger != "population")
                throw std::runtime_error("mechanisms.stress.trigger must be \"lineage\" or \"population\"");
            set_field(s, "patience", m.stress.patience);
            set_field(s, "growth", m.stress.growth);
            set_field(s, "max_multiplier", m.stress.max_multiplier);
        } else if (name == "hypermutation") {
            check_keys("mechanisms." + name, s, {"enabled", "initial_rate", "min_rate", "max_rate", "p_change", "change_sigma"});
            set_field(s, "enabled", m.hypermutation.enabled);
            set_field(s, "initial_rate", m.hypermutation.initial_rate);
            set_field(s, "min_rate", m.hypermutation.min_rate);
            set_field(s, "max_rate", m.hypermutation.max_rate);
            set_field(s, "p_change", m.hypermutation.p_change);
            set_field(s, "change_sigma", m.hypermutation.change_sigma);
        } else if (name == "transfer") {
            check_keys("mechanisms." + name, s, {"enabled", "rate", "max_segment"});
            set_field(s, "enabled", m.transfer.enabled);
            set_field(s, "rate", m.transfer.rate);
            set_field(s, "max_segment", m.transfer.max_segment);
        } else if (name == "duplication") {
            check_keys("mechanisms." + name, s, {"enabled", "rate", "max_segment"});
            set_field(s, "enabled", m.duplication.enabled);
            set_field(s, "rate", m.duplication.rate);
            set_field(s, "max_segment", m.duplication.max_segment);
        } else if (name == "spatial") {
            check_keys("mechanisms." + name, s, {"enabled", "width", "radius"});
            set_field(s, "enabled", m.spatial.enabled);
            set_field(s, "width", m.spatial.width);
            set_field(s, "radius", m.spatial.radius);
        } else if (name == "curriculum") {
            check_keys("mechanisms." + name, s, {"enabled", "bands", "refresh"});
            set_field(s, "enabled", m.curriculum.enabled);
            set_field(s, "bands", m.curriculum.bands);
            set_field(s, "refresh", m.curriculum.refresh);
        } else {
            throw std::runtime_error("unknown mechanism '" + name + "'");
        }
    }
    if (m.curriculum.enabled && !m.spatial.enabled)
        throw std::runtime_error("mechanisms.curriculum requires mechanisms.spatial to be enabled");
}

json seeding_to_json(const EngineConfig::Seeding& s) {
    json map = json::object();
    for (const auto& [from, to] : s.channel_map) map[from] = to;
    return json{{"strains", s.strains},
                {"library", s.library},
                {"random_fraction", s.random_fraction},
                {"retain_weight", s.retain_weight},
                {"channel_map", map}};
}

void seeding_from_json(EngineConfig::Seeding& s, const json& j) {
    check_keys("seeding", j, {"strains", "library", "random_fraction", "retain_weight", "channel_map"});
    if (j.contains("strains")) {
        s.strains.clear();
        for (const auto& x : j.at("strains")) s.strains.push_back(x.get<std::string>());
    }
    set_field(j, "library", s.library);
    set_field(j, "random_fraction", s.random_fraction);
    set_field(j, "retain_weight", s.retain_weight);
    if (s.retain_weight < 0 || s.retain_weight >= 1) throw std::runtime_error("seeding.retain_weight must be in [0, 1)");
    if (j.contains("channel_map")) {
        s.channel_map.clear();
        for (const auto& [from, to] : j.at("channel_map").items()) s.channel_map.emplace_back(from, to.get<std::string>());
    }
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
}  // namespace

json config_to_json(const EngineConfig& c) {
    json ops = json::array();
    for (Op op : c.ops) ops.push_back(std::string(op_info(op).name));
    const auto& v = c.variation;
    return json{
        {"seed", c.seed},
        {"population", c.population},
        {"generations", c.generations},
        {"elitism", c.elitism},
        {"selection", to_string(c.selection)},
        {"tournament_size", c.tournament_size},
        {"registers", c.num_registers},
        {"ops", ops},
        {"max_steps", c.limits.max_steps},
        {"max_loop_iterations", c.limits.max_loop_iterations},
        {"max_loop_body", c.limits.max_loop_body},
        {"const_min", c.const_min},
        {"const_max", c.const_max},
        {"length_cost", c.length_cost},
        {"time_cost", c.time_cost},
        {"threads", c.threads},
        {"stop_when_solved", c.stop_when_solved},
        {"max_program_evaluations", c.max_program_evaluations},
        {"validation_fraction", c.validation_fraction},
        {"max_case_evaluations", c.max_case_evaluations},
        {"downsample", c.downsample},
        {"downsample_min_cases", c.downsample_min_cases},
        {"cache_duplicates", c.cache_duplicates},
        {"constant_tuning",
         {{"enabled", c.constant_tuning.enabled},
          {"top", c.constant_tuning.top},
          {"steps", c.constant_tuning.steps},
          {"sigma", c.constant_tuning.sigma}}},
        {"seeding", seeding_to_json(c.seeding)},
        {"mechanisms", mechanisms_to_json(c.mechanisms)},
        {"variation",
         {{"init_min_length", v.init_min_length},
          {"init_max_length", v.init_max_length},
          {"max_length", v.max_length},
          {"sense_weight", v.sense_weight},
          {"act_weight", v.act_weight},
          {"p_crossover", v.p_crossover},
          {"p_mutation", v.p_mutation},
          {"w_insert", v.w_insert},
          {"w_delete", v.w_delete},
          {"w_point", v.w_point},
          {"w_constant", v.w_constant},
          {"constant_sigma", v.constant_sigma},
          {"effective_mutation", v.effective_mutation}}},
    };
}

void config_from_json(EngineConfig& c, const json& j) {
    for (const auto& [key, val] : j.items()) {
        if (key == "seed") c.seed = val.get<std::uint64_t>();
        else if (key == "population") c.population = val.get<int>();
        else if (key == "generations") c.generations = val.get<int>();
        else if (key == "elitism") c.elitism = val.get<int>();
        else if (key == "selection") {
            const auto s = val.get<std::string>();
            if (s == "tournament") c.selection = SelectionMethod::Tournament;
            else if (s == "lexicase") c.selection = SelectionMethod::Lexicase;
            else throw std::runtime_error("selection must be tournament or lexicase");
        } else if (key == "tournament_size") c.tournament_size = val.get<int>();
        else if (key == "registers") c.num_registers = val.get<int>();
        else if (key == "ops") c.ops = ops_from_json(val);
        else if (key == "max_steps") c.limits.max_steps = val.get<int>();
        else if (key == "max_loop_iterations") c.limits.max_loop_iterations = val.get<int>();
        else if (key == "max_loop_body") c.limits.max_loop_body = val.get<int>();
        else if (key == "const_min") c.const_min = val.get<double>();
        else if (key == "const_max") c.const_max = val.get<double>();
        else if (key == "length_cost") c.length_cost = val.get<double>();
        else if (key == "time_cost") c.time_cost = val.get<double>();
        else if (key == "threads") c.threads = val.get<int>();
        else if (key == "stop_when_solved") c.stop_when_solved = val.get<bool>();
        else if (key == "max_program_evaluations") c.max_program_evaluations = val.get<long>();
        else if (key == "mechanisms") mechanisms_from_json(c.mechanisms, val);
        else if (key == "validation_fraction") c.validation_fraction = val.get<double>();
        else if (key == "max_case_evaluations") c.max_case_evaluations = val.get<long>();
        else if (key == "downsample") {
            c.downsample = val.get<double>();
            if (!(c.downsample > 0 && c.downsample <= 1)) throw std::runtime_error("downsample must be in (0, 1]");
        } else if (key == "downsample_min_cases") c.downsample_min_cases = val.get<int>();
        else if (key == "cache_duplicates") c.cache_duplicates = val.get<bool>();
        else if (key == "constant_tuning") {
            if (val.is_boolean()) c.constant_tuning.enabled = val.get<bool>();
            else {
                check_keys("constant_tuning", val, {"enabled", "top", "steps", "sigma"});
                set_field(val, "enabled", c.constant_tuning.enabled);
                set_field(val, "top", c.constant_tuning.top);
                set_field(val, "steps", c.constant_tuning.steps);
                set_field(val, "sigma", c.constant_tuning.sigma);
            }
        }
        else if (key == "seeding") seeding_from_json(c.seeding, val);
        else if (key == "variation") {
            auto& v = c.variation;
            for (const auto& [k, x] : val.items()) {
                if (k == "init_min_length") v.init_min_length = x.get<int>();
                else if (k == "init_max_length") v.init_max_length = x.get<int>();
                else if (k == "max_length") v.max_length = x.get<int>();
                else if (k == "sense_weight") v.sense_weight = x.get<double>();
                else if (k == "act_weight") v.act_weight = x.get<double>();
                else if (k == "p_crossover") v.p_crossover = x.get<double>();
                else if (k == "p_mutation") v.p_mutation = x.get<double>();
                else if (k == "w_insert") v.w_insert = x.get<double>();
                else if (k == "w_delete") v.w_delete = x.get<double>();
                else if (k == "w_point") v.w_point = x.get<double>();
                else if (k == "w_constant") v.w_constant = x.get<double>();
                else if (k == "constant_sigma") v.constant_sigma = x.get<double>();
                else if (k == "effective_mutation") v.effective_mutation = x.get<bool>();
                else throw std::runtime_error("unknown variation key '" + k + "'");
            }
        } else
            throw std::runtime_error("unknown config key '" + key + "'");
    }
}

namespace {
json metrics_json(const std::vector<Metric>& ms) {
    json j = json::object();
    for (const auto& m : ms) j[m.name] = m.value;
    return j;
}
}  // namespace

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
                {"best_train_metrics", metrics_json(r.best_train_metrics)},
                {"population_stats", metrics_json(r.population_stats)}};
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
                {"train_metrics", metrics_json(r.train_metrics)},
                {"test_metrics", metrics_json(r.test_metrics)},
                {"program", program_to_json(spec, r.best)}};
}

}  // namespace adaptogen
