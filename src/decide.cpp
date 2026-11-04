#include "adaptogen/decide.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>
#include <set>

#include "adaptogen/analysis.hpp"
#include "adaptogen/problems/decisions.hpp"
#include "adaptogen/report.hpp"
#include "adaptogen/serialize.hpp"

namespace adaptogen {

using nlohmann::json;

Decision decide(const Strain& strain, const json& options, const json& context) {
    if (!options.is_array() || options.empty()) throw std::runtime_error("options must be a non-empty array of objects");
    const MachineSpec spec = strain_machine(strain);
    const Program program = program_from_json(spec, strain.program);
    const auto& ch = spec.channels;
    // Resolve a given key to a channel: exact name, or with an "option."/"item."/"context." prefix.
    auto resolve = [&](const std::string& key, bool is_context) -> int {
        const std::vector<std::string> tries = is_context ? std::vector<std::string>{key, "context." + key}
                                                          : std::vector<std::string>{key, "option." + key, "item." + key};
        for (const auto& t : tries)
            for (std::size_t i = 0; i < ch.size(); ++i)
                if (ch[i].name == t && ch[i].kind == ChannelKind::Sensor) return static_cast<int>(i);
        return -1;
    };
    Decision d;
    std::set<std::string> unknown;
    std::vector<std::vector<double>> values(options.size(), std::vector<double>(ch.size(), 0.0));
    std::vector<std::vector<bool>> given(options.size(), std::vector<bool>(ch.size(), false));
    for (std::size_t o = 0; o < options.size(); ++o) {
        for (const auto& [k, v] : options[o].items()) {
            const int c = resolve(k, false);
            if (c < 0) unknown.insert(k);
            else values[o][static_cast<std::size_t>(c)] = v.get<double>(), given[o][static_cast<std::size_t>(c)] = true;
        }
        for (const auto& [k, v] : context.items()) {
            const int c = resolve(k, true);
            if (c < 0) unknown.insert(k);
            else values[o][static_cast<std::size_t>(c)] = v.get<double>(), given[o][static_cast<std::size_t>(c)] = true;
        }
    }
    // Derived channels (decision strains): option count and per-decision min/max of each feature.
    for (std::size_t i = 0; i < ch.size(); ++i) {
        const std::string& n = ch[i].name;
        if (n == "options.count" || n == "items.remaining") {
            for (auto& v : values) v[i] = static_cast<double>(options.size());
            for (auto&& g : given) g[i] = true;
        } else if (n.rfind("options.", 0) == 0 && (n.size() > 4 && (n.substr(n.size() - 4) == ".min" || n.substr(n.size() - 4) == ".max"))) {
            const std::string feat = "option." + n.substr(8, n.size() - 12);
            const int f = resolve(feat, false);
            if (f < 0) continue;
            double agg = n.substr(n.size() - 4) == ".min" ? std::numeric_limits<double>::infinity() : -std::numeric_limits<double>::infinity();
            for (const auto& v : values) agg = n.substr(n.size() - 4) == ".min" ? std::min(agg, v[static_cast<std::size_t>(f)]) : std::max(agg, v[static_cast<std::size_t>(f)]);
            for (auto& v : values) v[i] = agg;
            for (auto&& g : given) g[i] = true;
        }
    }
    // Channels the effective code reads but nobody provided.
    const auto mask = effective_mask(spec, program, strain.num_outputs);
    std::set<std::string> missing;
    for (std::size_t i = 0; i < program.code.size(); ++i)
        if (mask[i] && program.code[i].op == Op::Sense && program.code[i].channel < ch.size() && !given[0][program.code[i].channel])
            missing.insert(ch[program.code[i].channel].name);
    d.unknown_keys.assign(unknown.begin(), unknown.end());
    d.missing.assign(missing.begin(), missing.end());

    struct Env : Environment {
        const std::vector<double>* v;
        double sense(int c, const double*, int) override { return c >= 0 && c < static_cast<int>(v->size()) ? (*v)[static_cast<std::size_t>(c)] : 0.0; }
    };
    std::array<double, 256> regs{};
    for (const auto& v : values) {
        Env env;
        env.v = &v;
        regs.fill(0.0);
        run(spec, program, env, std::span<double>(regs.data(), static_cast<std::size_t>(spec.num_registers)));
        d.scores.push_back(std::isfinite(regs[0]) ? regs[0] : -std::numeric_limits<double>::infinity());
    }
    d.ranking.resize(d.scores.size());
    std::iota(d.ranking.begin(), d.ranking.end(), 0);
    std::stable_sort(d.ranking.begin(), d.ranking.end(), [&](int a, int b) { return d.scores[static_cast<std::size_t>(a)] > d.scores[static_cast<std::size_t>(b)]; });
    d.choice = d.ranking.front();
    d.margin = decision_margin(d.scores);
    if (strain.extras.contains("decision") && strain.extras.at("decision").contains("calibration")) {
        for (const auto& bin : strain.extras.at("decision").at("calibration"))
            if (d.margin >= bin.value("min_margin", 0.0) - 1e-12) d.confidence = bin.value("accuracy", 0.0);
        if (d.confidence < 0 && !strain.extras.at("decision").at("calibration").empty())
            d.confidence = strain.extras.at("decision").at("calibration")[0].value("accuracy", 0.0);
    }
    return d;
}

json to_json(const Decision& d) {
    json j{{"choice", d.choice}, {"scores", d.scores}, {"ranking", d.ranking}, {"margin", d.margin}};
    if (d.confidence >= 0) j["confidence"] = d.confidence;
    if (!d.unknown_keys.empty()) j["ignored_keys"] = d.unknown_keys;
    if (!d.missing.empty()) j["missing_channels"] = d.missing;
    return j;
}

}  // namespace adaptogen
