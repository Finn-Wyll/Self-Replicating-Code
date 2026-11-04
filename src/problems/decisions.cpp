#include "adaptogen/problems/decisions.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <limits>
#include <map>
#include <numeric>
#include <sstream>
#include <stdexcept>

#include "adaptogen/random.hpp"

namespace adaptogen {

using nlohmann::json;

namespace {

std::vector<std::string> split_csv(const std::string& line) {
    std::vector<std::string> out;
    std::string cell;
    bool quoted = false;
    for (char ch : line) {
        if (ch == '"') quoted = !quoted;
        else if (ch == ',' && !quoted) {
            out.push_back(cell);
            cell.clear();
        } else if (ch != '\r') cell += ch;
    }
    out.push_back(cell);
    for (auto& c : out) {
        const auto b = c.find_first_not_of(" \t"), e = c.find_last_not_of(" \t");
        c = b == std::string::npos ? "" : c.substr(b, e - b + 1);
    }
    return out;
}

double number(const std::string& s, const std::string& where) {
    try {
        std::size_t used = 0;
        const double v = std::stod(s, &used);
        if (used != s.size()) throw std::invalid_argument("");
        return v;
    } catch (...) {
        throw std::runtime_error(where + ": not a number: '" + s + "'");
    }
}

int argmax(const std::vector<double>& v) {
    int best = 0;
    for (int i = 1; i < static_cast<int>(v.size()); ++i)
        if (v[static_cast<std::size_t>(i)] > v[static_cast<std::size_t>(best)]) best = i;
    return best;
}

}  // namespace

DecisionData load_decision_data(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot open decision data '" + path + "'");
    DecisionData d;
    const bool is_json = path.size() > 5 && path.substr(path.size() - 5) == ".json";
    if (is_json) {
        const json j = json::parse(in);
        std::map<std::string, int> fidx, cidx;
        for (const auto& c : j.at("cases")) {
            const json ctx = c.value("context", json::object());  // must outlive the loop
            for (const auto& [k, v] : ctx.items()) {
                (void)v;
                if (!cidx.count(k)) cidx[k] = static_cast<int>(d.context.size()), d.context.push_back(k);
            }
            for (const auto& o : c.at("options"))
                for (const auto& [k, v] : o.items()) {
                    (void)v;
                    if (k != "reward" && k != "option" && !fidx.count(k)) fidx[k] = static_cast<int>(d.features.size()), d.features.push_back(k);
                }
        }
        for (const auto& c : j.at("cases")) {
            DecisionCase dc;
            dc.context.assign(d.context.size(), 0.0);
            const json ctx = c.value("context", json::object());
            for (const auto& [k, v] : ctx.items()) dc.context[static_cast<std::size_t>(cidx[k])] = v.get<double>();
            for (const auto& o : c.at("options")) {
                std::vector<double> f(d.features.size(), 0.0);
                for (const auto& [k, v] : o.items())
                    if (fidx.count(k)) f[static_cast<std::size_t>(fidx[k])] = v.get<double>();
                dc.options.push_back(std::move(f));
                dc.rewards.push_back(o.value("reward", 0.0));
                dc.labels.push_back(o.contains("option") ? o.at("option").dump() : "");
            }
            if (dc.options.size() >= 2) d.cases.push_back(std::move(dc));
        }
        return d;
    }
    std::string line;
    if (!std::getline(in, line)) throw std::runtime_error("empty decision data '" + path + "'");
    const auto header = split_csv(line);
    int case_col = -1, reward_col = -1, label_col = -1;
    std::vector<int> fcols, ccols;
    for (int k = 0; k < static_cast<int>(header.size()); ++k) {
        const std::string& h = header[static_cast<std::size_t>(k)];
        if (h == "case") case_col = k;
        else if (h == "reward") reward_col = k;
        else if (h == "option") label_col = k;
        else if (h.rfind("context.", 0) == 0) ccols.push_back(k), d.context.push_back(h.substr(8));
        else fcols.push_back(k), d.features.push_back(h);
    }
    if (case_col < 0 || reward_col < 0) throw std::runtime_error(path + ": needs 'case' and 'reward' columns");
    if (fcols.empty()) throw std::runtime_error(path + ": needs at least one option feature column");
    std::map<std::string, std::size_t> index;
    int lineno = 1;
    while (std::getline(in, line)) {
        ++lineno;
        if (line.find_first_not_of(" \t\r") == std::string::npos) continue;
        const auto cells = split_csv(line);
        const std::string where = path + ":" + std::to_string(lineno);
        if (cells.size() != header.size()) throw std::runtime_error(where + ": wrong number of columns");
        const std::string& cid = cells[static_cast<std::size_t>(case_col)];
        auto it = index.find(cid);
        if (it == index.end()) {
            it = index.emplace(cid, d.cases.size()).first;
            DecisionCase dc;
            for (int k : ccols) dc.context.push_back(number(cells[static_cast<std::size_t>(k)], where));
            d.cases.push_back(std::move(dc));
        }
        DecisionCase& dc = d.cases[it->second];
        std::vector<double> f;
        for (int k : fcols) f.push_back(number(cells[static_cast<std::size_t>(k)], where));
        dc.options.push_back(std::move(f));
        dc.rewards.push_back(number(cells[static_cast<std::size_t>(reward_col)], where));
        dc.labels.push_back(label_col >= 0 ? cells[static_cast<std::size_t>(label_col)] : "");
    }
    std::erase_if(d.cases, [](const DecisionCase& c) { return c.options.size() < 2; });
    if (d.cases.size() < 4) throw std::runtime_error(path + ": needs at least 4 decisions with 2+ options");
    return d;
}

std::unique_ptr<DecisionProblem> DecisionProblem::from_params(const json& params, std::uint64_t seed) {
    if (!params.contains("data")) throw std::runtime_error("decisions needs {\"data\": \"file.csv\"} (or a list of files)");
    std::vector<std::string> files;
    if (params.at("data").is_array())
        for (const auto& f : params.at("data")) files.push_back(f.get<std::string>());
    else files.push_back(params.at("data").get<std::string>());
    DecisionData all;
    for (const auto& f : files) {
        DecisionData d = load_decision_data(f);
        if (all.features.empty()) {
            all.features = d.features;
            all.context = d.context;
        } else if (d.features != all.features || d.context != all.context) {
            throw std::runtime_error(f + ": its columns differ from the first data file's");
        }
        all.cases.insert(all.cases.end(), d.cases.begin(), d.cases.end());
    }
    Rng rng(seed ^ 0xdec1510eULL);
    std::vector<std::size_t> idx(all.cases.size());
    std::iota(idx.begin(), idx.end(), 0);
    for (std::size_t i = idx.size(); i > 1; --i) std::swap(idx[i - 1], idx[rng.below(i)]);
    const double frac = std::clamp(params.value("test_fraction", 0.25), 0.05, 0.9);
    const std::size_t n_test = std::max<std::size_t>(1, static_cast<std::size_t>(frac * static_cast<double>(idx.size())));
    DecisionData train, test;
    train.features = test.features = all.features;
    train.context = test.context = all.context;
    for (std::size_t k = 0; k < idx.size(); ++k) (k < n_test ? test : train).cases.push_back(all.cases[idx[k]]);
    std::string name = "decisions:" + files.front() + (files.size() > 1 ? " (+" + std::to_string(files.size() - 1) + " more)" : "");
    return std::make_unique<DecisionProblem>(name, std::move(train), std::move(test));
}

DecisionProblem::DecisionProblem(std::string name, DecisionData train, DecisionData test)
    : name_(std::move(name)), train_(std::move(train)), test_(std::move(test)) {
    fit_baselines();
}

std::string DecisionProblem::description() const {
    return "Choose the best option from logged decisions (" + std::to_string(train_.cases.size()) + " training, " +
           std::to_string(test_.cases.size()) + " held-out); simple solutions for comparison: best single feature ('" +
           (sf_sign_ > 0 ? "highest " : "lowest ") + train_.features[static_cast<std::size_t>(sf_feature_)] + "') and a linear model";
}

std::vector<Channel> DecisionProblem::channels() const {
    std::vector<Channel> ch;
    for (const auto& f : train_.features) ch.push_back({"option." + f, ChannelKind::Sensor, 0, "this option's " + f, ""});
    for (const auto& c : train_.context) ch.push_back({"context." + c, ChannelKind::Sensor, 0, "situation: " + c, ""});
    ch.push_back({"options.count", ChannelKind::Sensor, 0, "number of options in this decision", "options"});
    for (const auto& f : train_.features) {
        ch.push_back({"options." + f + ".min", ChannelKind::Sensor, 0, "lowest " + f + " among the options", ""});
        ch.push_back({"options." + f + ".max", ChannelKind::Sensor, 0, "highest " + f + " among the options", ""});
    }
    return ch;
}

std::vector<double> DecisionProblem::option_scores(Runner& runner, const Program& program, const DecisionCase& c) const {
    const std::size_t F = train_.features.size(), K = train_.context.size();
    std::vector<double> lo(F, std::numeric_limits<double>::infinity()), hi(F, -std::numeric_limits<double>::infinity());
    for (const auto& o : c.options)
        for (std::size_t f = 0; f < F; ++f) lo[f] = std::min(lo[f], o[f]), hi[f] = std::max(hi[f], o[f]);
    struct Env : Environment {
        const std::vector<double>* opt;
        const DecisionCase* c;
        const std::vector<double>*lo, *hi;
        std::size_t F, K;
        double sense(int ch, const double*, int) override {
            const std::size_t u = static_cast<std::size_t>(ch);
            if (u < F) return (*opt)[u];
            if (u < F + K) return c->context[u - F];
            if (u == F + K) return static_cast<double>(c->options.size());
            const std::size_t r = u - F - K - 1;
            return (r % 2 == 0 ? *lo : *hi)[r / 2];
        }
    };
    std::vector<double> scores;
    std::array<double, 256> regs{};
    for (const auto& o : c.options) {
        Env env;
        env.opt = &o;
        env.c = &c;
        env.lo = &lo;
        env.hi = &hi;
        env.F = F;
        env.K = K;
        regs.fill(0.0);
        runner.run(program, env, std::span<double>(regs.data(), static_cast<std::size_t>(runner.spec().num_registers)));
        scores.push_back(std::isfinite(regs[0]) ? regs[0] : -std::numeric_limits<double>::infinity());
    }
    return scores;
}

double DecisionProblem::evaluate_choice(const DecisionCase& c, int choice) const {
    const double mx = *std::max_element(c.rewards.begin(), c.rewards.end());
    const double mn = *std::min_element(c.rewards.begin(), c.rewards.end());
    return mx > mn ? (c.rewards[static_cast<std::size_t>(choice)] - mn) / (mx - mn) : 1.0;
}

double DecisionProblem::evaluate_case(Runner& runner, const Program& program, Split split, std::size_t index) const {
    const DecisionCase& c = data(split).cases[index];
    return evaluate_choice(c, argmax(option_scores(runner, program, c)));
}

int DecisionProblem::baseline_single_feature(const DecisionCase& c) const {
    std::vector<double> v;
    for (const auto& o : c.options) v.push_back(sf_sign_ * o[static_cast<std::size_t>(sf_feature_)]);
    return argmax(v);
}

int DecisionProblem::baseline_linear(const DecisionCase& c) const {
    std::vector<double> v;
    for (const auto& o : c.options) {
        double s = 0;
        for (std::size_t f = 0; f < o.size(); ++f) s += lin_w_[f] * (o[f] - mean_[f]) / sd_[f];
        v.push_back(s);
    }
    return argmax(v);
}

void DecisionProblem::fit_baselines() {
    const std::size_t F = train_.features.size();
    // Best single-feature rule on the training cases.
    {
        double bt = -1;
        int bf = 0, bs = 1;
        for (std::size_t f = 0; f < F; ++f)
            for (int sign : {1, -1}) {
                sf_feature_ = static_cast<int>(f);
                sf_sign_ = sign;
                double total = 0;
                for (const auto& c : train_.cases) total += evaluate_choice(c, baseline_single_feature(c));
                if (total > bt) bt = total, bf = static_cast<int>(f), bs = sign;
            }
        sf_feature_ = bf;
        sf_sign_ = bs;
    }
    // Linear scoring model: conditional logit on standardised features, trained to put the
    // probability mass on the best option(s).
    mean_.assign(F, 0.0);
    sd_.assign(F, 0.0);
    double n = 0;
    for (const auto& c : train_.cases)
        for (const auto& o : c.options) {
            for (std::size_t f = 0; f < F; ++f) mean_[f] += o[f];
            n += 1;
        }
    for (auto& m : mean_) m /= std::max(1.0, n);
    for (const auto& c : train_.cases)
        for (const auto& o : c.options)
            for (std::size_t f = 0; f < F; ++f) sd_[f] += (o[f] - mean_[f]) * (o[f] - mean_[f]);
    for (auto& s : sd_) s = std::max(1e-9, std::sqrt(s / std::max(1.0, n)));
    lin_w_.assign(F, 0.0);
    std::vector<double> grad(F);
    for (int epoch = 0; epoch < 400; ++epoch) {
        std::fill(grad.begin(), grad.end(), 0.0);
        for (const auto& c : train_.cases) {
            const std::size_t K = c.options.size();
            std::vector<double> z(K), p(K), t(K);
            const double mx = *std::max_element(c.rewards.begin(), c.rewards.end());
            double tz = 0;
            for (std::size_t k = 0; k < K; ++k) {
                for (std::size_t f = 0; f < F; ++f) z[k] += lin_w_[f] * (c.options[k][f] - mean_[f]) / sd_[f];
                t[k] = c.rewards[k] >= mx - 1e-12 ? 1.0 : 0.0;
                tz += t[k];
            }
            const double zmax = *std::max_element(z.begin(), z.end());
            double zs = 0;
            for (std::size_t k = 0; k < K; ++k) zs += p[k] = std::exp(z[k] - zmax);
            for (std::size_t k = 0; k < K; ++k) {
                const double err = p[k] / zs - t[k] / tz;
                for (std::size_t f = 0; f < F; ++f) grad[f] += err * (c.options[k][f] - mean_[f]) / sd_[f];
            }
        }
        for (std::size_t f = 0; f < F; ++f)
            lin_w_[f] -= 0.5 * (grad[f] / static_cast<double>(std::max<std::size_t>(1, train_.cases.size())) + 1e-3 * lin_w_[f]);
    }
}

std::vector<Metric> DecisionProblem::metrics(const MachineSpec& spec, const Program& program, Split split) const {
    const auto& cases = data(split).cases;
    Runner runner(spec);
    double acc = 0, regret = 0, sf_acc = 0, lin_acc = 0, rnd = 0, sf_reg = 0, lin_reg = 0;
    for (const auto& c : cases) {
        const double mx = *std::max_element(c.rewards.begin(), c.rewards.end());
        auto hit = [&](int k) { return c.rewards[static_cast<std::size_t>(k)] >= mx - 1e-12 ? 1.0 : 0.0; };
        const int mine = argmax(option_scores(runner, program, c));
        acc += hit(mine);
        regret += mx - c.rewards[static_cast<std::size_t>(mine)];
        const int sf = baseline_single_feature(c), lin = baseline_linear(c);
        sf_acc += hit(sf);
        lin_acc += hit(lin);
        sf_reg += mx - c.rewards[static_cast<std::size_t>(sf)];
        lin_reg += mx - c.rewards[static_cast<std::size_t>(lin)];
        double best_count = 0;
        for (double r : c.rewards) best_count += r >= mx - 1e-12;
        rnd += best_count / static_cast<double>(c.rewards.size());
    }
    const double n = static_cast<double>(std::max<std::size_t>(1, cases.size()));
    return {{"accuracy", acc / n},
            {"best_single_feature_accuracy", sf_acc / n},
            {"linear_model_accuracy", lin_acc / n},
            {"random_accuracy", rnd / n},
            {"mean_regret", regret / n},
            {"best_single_feature_regret", sf_reg / n},
            {"linear_model_regret", lin_reg / n}};
}

bool DecisionProblem::solved(const MachineSpec& spec, const Program& program, Split split) const {
    return metrics(spec, program, split)[0].value >= 1.0;
}

double decision_margin(const std::vector<double>& scores) {
    if (scores.size() < 2) return 1.0;
    std::vector<double> s = scores;
    for (double& v : s)
        if (!std::isfinite(v)) v = -1e300;
    std::sort(s.begin(), s.end(), std::greater<>());
    const double spread = s.front() - s.back();
    return spread > 0 ? std::clamp((s[0] - s[1]) / spread, 0.0, 1.0) : 0.0;
}

json DecisionProblem::strain_extras(const MachineSpec& spec, const Program& program) const {
    // Calibrate confidence: how often the rule picks a best option, by how clearly it prefers it.
    Runner runner(spec);
    std::vector<std::pair<double, double>> mh;  // (margin, hit)
    for (const auto& c : train_.cases) {
        const auto sc = option_scores(runner, program, c);
        const double mx = *std::max_element(c.rewards.begin(), c.rewards.end());
        mh.emplace_back(decision_margin(sc), c.rewards[static_cast<std::size_t>(argmax(sc))] >= mx - 1e-12 ? 1.0 : 0.0);
    }
    std::sort(mh.begin(), mh.end());
    json bins = json::array();
    const std::size_t nb = std::min<std::size_t>(5, std::max<std::size_t>(1, mh.size() / 10));
    for (std::size_t b = 0; b < nb; ++b) {
        const std::size_t lo = b * mh.size() / nb, hi = (b + 1) * mh.size() / nb;
        double hits = 0;
        for (std::size_t i = lo; i < hi; ++i) hits += mh[i].second;
        bins.push_back({{"min_margin", lo < mh.size() ? mh[lo].first : 0.0}, {"accuracy", hits / static_cast<double>(std::max<std::size_t>(1, hi - lo))},
                        {"cases", hi - lo}});
    }
    return json{{"decision",
                 {{"features", train_.features},
                  {"context", train_.context},
                  {"calibration", bins},
                  {"simple_solutions",
                   {{"best_single_feature", (sf_sign_ > 0 ? "highest " : "lowest ") + train_.features[static_cast<std::size_t>(sf_feature_)]},
                    {"linear_model_weights", lin_w_},
                    {"linear_model_feature_means", mean_},
                    {"linear_model_feature_sds", sd_}}}}}};
}

}  // namespace adaptogen
