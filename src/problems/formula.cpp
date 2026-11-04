#include "adaptogen/problems/formula.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <numeric>
#include <sstream>
#include <stdexcept>

namespace adaptogen {

RegressionMetrics regression_metrics(const std::vector<double>& y, const std::vector<double>& pred) {
    RegressionMetrics m;
    const std::size_t n = y.size();
    if (n == 0) return m;
    const double mean = std::accumulate(y.begin(), y.end(), 0.0) / static_cast<double>(n);
    double ss_res = 0, ss_tot = 0, abs = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const double e = std::isfinite(pred[i]) ? y[i] - pred[i] : 1e15;
        ss_res += e * e;
        abs += std::fabs(e);
        ss_tot += (y[i] - mean) * (y[i] - mean);
    }
    m.mae = abs / static_cast<double>(n);
    m.rmse = std::sqrt(ss_res / static_cast<double>(n));
    m.r2 = ss_tot > 0 ? 1.0 - ss_res / ss_tot : (ss_res == 0 ? 1.0 : 0.0);
    const double sd = std::sqrt(ss_tot / static_cast<double>(n));
    m.nrmse = sd > 0 ? m.rmse / sd : m.rmse;
    return m;
}

namespace {

Dataset sample_uniform(Rng& rng, int n, const std::vector<std::string>& vars, double lo, double hi,
                       const std::function<double(const std::vector<double>&)>& f) {
    Dataset d;
    d.variables = vars;
    for (int i = 0; i < n; ++i) {
        std::vector<double> x(vars.size());
        for (auto& v : x) v = rng.uniform(lo, hi);
        d.y.push_back(f(x));
        d.x.push_back(std::move(x));
    }
    return d;
}

Dataset grid1(double lo, double hi, double step, const std::function<double(const std::vector<double>&)>& f) {
    Dataset d;
    d.variables = {"x"};
    for (double v = lo; v <= hi + 1e-9; v += step) {
        d.x.push_back({v});
        d.y.push_back(f(d.x.back()));
    }
    return d;
}

Dataset grid2(double lo, double hi, double step, const std::function<double(const std::vector<double>&)>& f) {
    Dataset d;
    d.variables = {"x", "y"};
    for (double a = lo; a <= hi + 1e-9; a += step)
        for (double b = lo; b <= hi + 1e-9; b += step) {
            d.x.push_back({a, b});
            d.y.push_back(f(d.x.back()));
        }
    return d;
}

double harmonic(const std::vector<double>& x) {
    double s = 0;
    for (int i = 1; i <= static_cast<int>(std::lround(x[0])); ++i) s += 1.0 / i;
    return s;
}

}  // namespace

const std::vector<FormulaTarget>& formula_targets() {
    static const std::vector<FormulaTarget> targets = [] {
        std::vector<FormulaTarget> t;
        auto koza1 = [](const std::vector<double>& x) { double v = x[0]; return v * v * v * v + v * v * v + v * v + v; };
        t.push_back({"koza1", "x^4 + x^3 + x^2 + x   (x in [-1,1])",
                     [=](Rng& r) { return sample_uniform(r, 20, {"x"}, -1, 1, koza1); },
                     [=](Rng& r) { return sample_uniform(r, 200, {"x"}, -1, 1, koza1); }});
        auto ng5 = [](const std::vector<double>& x) { return std::sin(x[0] * x[0]) * std::cos(x[0]) - 1.0; };
        t.push_back({"nguyen5", "sin(x^2) cos(x) - 1   (x in [-1,1])",
                     [=](Rng& r) { return sample_uniform(r, 20, {"x"}, -1, 1, ng5); },
                     [=](Rng& r) { return sample_uniform(r, 200, {"x"}, -1, 1, ng5); }});
        auto ng7 = [](const std::vector<double>& x) { return std::log(x[0] + 1) + std::log(x[0] * x[0] + 1); };
        t.push_back({"nguyen7", "ln(x+1) + ln(x^2+1)   (x in [0,2])",
                     [=](Rng& r) { return sample_uniform(r, 20, {"x"}, 0, 2, ng7); },
                     [=](Rng& r) { return sample_uniform(r, 200, {"x"}, 0, 2, ng7); }});
        auto ng10 = [](const std::vector<double>& x) { return 2 * std::sin(x[0]) * std::cos(x[1]); };
        t.push_back({"nguyen10", "2 sin(x) cos(y)   (x, y in [0,1])",
                     [=](Rng& r) { return sample_uniform(r, 100, {"x", "y"}, 0, 1, ng10); },
                     [=](Rng& r) { return sample_uniform(r, 500, {"x", "y"}, 0, 1, ng10); }});
        auto pagie = [](const std::vector<double>& x) {
            return 1 / (1 + std::pow(x[0], -4)) + 1 / (1 + std::pow(x[1], -4));
        };
        t.push_back({"pagie1", "1/(1+x^-4) + 1/(1+y^-4)   (x, y in [-5,5])",
                     [=](Rng&) { return grid2(-5, 5, 0.4, pagie); },
                     [=](Rng& r) { return sample_uniform(r, 1000, {"x", "y"}, -5, 5, pagie); }});
        t.push_back({"keijzer6", "sum_{i=1..x} 1/i   (train x=1..50, test x=1..120: extrapolation)",
                     [](Rng&) { return grid1(1, 50, 1, harmonic); },
                     [](Rng&) { return grid1(1, 120, 1, harmonic); }});
        return t;
    }();
    return targets;
}

Dataset load_csv(const std::string& path, const std::string& target) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot open data file '" + path + "'");
    auto split = [](const std::string& line) {
        std::vector<std::string> out;
        std::stringstream ss(line);
        for (std::string cell; std::getline(ss, cell, ',');) {
            const auto b = cell.find_first_not_of(" \t\r\"");
            const auto e = cell.find_last_not_of(" \t\r\"");
            out.push_back(b == std::string::npos ? "" : cell.substr(b, e - b + 1));
        }
        return out;
    };
    std::string line;
    if (!std::getline(in, line)) throw std::runtime_error("empty data file '" + path + "'");
    const auto header = split(line);
    if (header.size() < 2) throw std::runtime_error("data file needs at least one input and one target column");
    std::size_t tcol = header.size() - 1;
    if (!target.empty()) {
        auto it = std::find(header.begin(), header.end(), target);
        if (it == header.end()) throw std::runtime_error("target column '" + target + "' not found");
        tcol = static_cast<std::size_t>(it - header.begin());
    }
    Dataset d;
    for (std::size_t k = 0; k < header.size(); ++k)
        if (k != tcol) d.variables.push_back(header[k]);
    int lineno = 1;
    while (std::getline(in, line)) {
        ++lineno;
        if (line.find_first_not_of(" \t\r") == std::string::npos) continue;
        const auto cells = split(line);
        if (cells.size() != header.size())
            throw std::runtime_error(path + ":" + std::to_string(lineno) + ": wrong number of columns");
        std::vector<double> x;
        for (std::size_t k = 0; k < cells.size(); ++k) {
            double v;
            try {
                v = std::stod(cells[k]);
            } catch (...) {
                throw std::runtime_error(path + ":" + std::to_string(lineno) + ": not a number: " + cells[k]);
            }
            if (k == tcol) d.y.push_back(v);
            else x.push_back(v);
        }
        d.x.push_back(std::move(x));
    }
    if (d.size() < 4) throw std::runtime_error("data file needs at least 4 rows");
    return d;
}

FormulaProblem::FormulaProblem(std::string name, std::string description, Dataset train, Dataset test)
    : name_(std::move(name)), description_(std::move(description)), train_(std::move(train)), test_(std::move(test)) {}

std::unique_ptr<FormulaProblem> FormulaProblem::from_params(const nlohmann::json& params, std::uint64_t seed) {
    Rng rng(seed ^ 0x5eed5eedULL);
    Dataset train, test;
    std::string name, desc;
    if (params.contains("data")) {
        const std::string path = params.at("data").get<std::string>();
        Dataset all = load_csv(path, params.value("target_column", ""));
        const double frac = std::clamp(params.value("test_fraction", 0.25), 0.0, 0.9);
        std::vector<std::size_t> idx(all.size());
        std::iota(idx.begin(), idx.end(), 0);
        for (std::size_t i = idx.size(); i > 1; --i) std::swap(idx[i - 1], idx[rng.below(i)]);
        const std::size_t n_test = std::min(idx.size() - 1, static_cast<std::size_t>(frac * static_cast<double>(idx.size())));
        train.variables = test.variables = all.variables;
        for (std::size_t k = 0; k < idx.size(); ++k) {
            Dataset& d = k < n_test ? test : train;
            d.x.push_back(all.x[idx[k]]);
            d.y.push_back(all.y[idx[k]]);
        }
        if (test.size() == 0) test = train;
        name = "formula:" + path;
        desc = "Formula discovery from " + path;
    } else {
        const std::string target = params.value("target", "koza1");
        const FormulaTarget* t = nullptr;
        for (const auto& c : formula_targets())
            if (c.name == target) t = &c;
        if (!t) throw std::runtime_error("unknown formula target '" + target + "'");
        train = t->train(rng);
        test = t->test(rng);
        name = "formula:" + t->name;
        desc = "Formula discovery benchmark " + t->name + ": " + t->formula;
    }
    if (const double noise = params.value("noise", 0.0); noise > 0) {
        const double mean = std::accumulate(train.y.begin(), train.y.end(), 0.0) / static_cast<double>(train.size());
        double var = 0;
        for (double v : train.y) var += (v - mean) * (v - mean);
        const double sd = std::sqrt(var / static_cast<double>(train.size()));
        for (double& v : train.y) v += rng.normal() * noise * sd;
    }
    return std::make_unique<FormulaProblem>(name, desc, std::move(train), std::move(test));
}

std::vector<Channel> FormulaProblem::channels() const {
    std::vector<Channel> ch;
    for (const auto& v : train_.variables) ch.push_back({v, ChannelKind::Sensor, 0, "input variable " + v, ""});
    return ch;
}

namespace {
struct RowEnv : Environment {
    const std::vector<double>& x;
    explicit RowEnv(const std::vector<double>& row) : x(row) {}
    double sense(int channel, const double*, int) override {
        return channel >= 0 && channel < static_cast<int>(x.size()) ? x[static_cast<std::size_t>(channel)] : 0.0;
    }
};
}  // namespace

double FormulaProblem::predict(Runner& runner, const Program& program, const std::vector<double>& x) const {
    std::array<double, 256> regs{};
    RowEnv env(x);
    runner.run(program, env, std::span<double>(regs.data(), static_cast<std::size_t>(runner.spec().num_registers)));
    return regs[0];
}

double FormulaProblem::evaluate_case(Runner& runner, const Program& program, Split split, std::size_t i) const {
    const Dataset& d = data(split);
    const double err = std::fabs(d.y[i] - predict(runner, program, d.x[i]));
    return std::isfinite(err) ? -std::min(err, 1e12) : -1e12;
}

std::vector<Metric> FormulaProblem::metrics(const MachineSpec& spec, const Program& program, Split split) const {
    const Dataset& d = data(split);
    Runner runner(spec);
    std::vector<double> pred(d.size());
    for (std::size_t i = 0; i < d.size(); ++i) pred[i] = predict(runner, program, d.x[i]);
    const auto m = regression_metrics(d.y, pred);
    return {{"r2", m.r2}, {"rmse", m.rmse}, {"mae", m.mae}, {"nrmse", m.nrmse}};
}

bool FormulaProblem::solved(const MachineSpec& spec, const Program& program, Split split) const {
    for (const auto& m : metrics(spec, program, split))
        if (m.name == "nrmse") return m.value < kSolvedNrmse;
    return false;
}

}  // namespace adaptogen
