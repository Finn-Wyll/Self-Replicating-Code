#include "adaptogen/problems/lists.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace adaptogen {

const char* to_string(ListProblem::Task t) {
    switch (t) {
        case ListProblem::Task::Sum: return "sum";
        case ListProblem::Task::Max: return "max";
        case ListProblem::Task::CountPositive: return "count_positive";
    }
    return "?";
}

std::unique_ptr<ListProblem> ListProblem::from_params(const nlohmann::json& params, std::uint64_t seed) {
    const std::string t = params.value("task", "sum");
    Task task;
    if (t == "sum") task = Task::Sum;
    else if (t == "max") task = Task::Max;
    else if (t == "count_positive") task = Task::CountPositive;
    else throw std::runtime_error("unknown list task '" + t + "' (sum, max, count_positive)");
    const int range = std::max(1, params.value("value_range", 9));
    Rng rng(seed ^ 0x11575eedULL);
    auto make = [&](int cases, int max_len) {
        std::vector<std::vector<double>> out;
        for (int i = 0; i < cases; ++i) {
            std::vector<double> l(static_cast<std::size_t>(rng.range(1, std::max(1, max_len))));
            for (double& v : l) v = rng.range(-range, range);
            out.push_back(std::move(l));
        }
        return out;
    };
    auto train = make(params.value("train_cases", 100), params.value("train_max_length", 20));
    auto test = make(params.value("test_cases", 200), params.value("test_max_length", 50));
    return std::make_unique<ListProblem>(task, std::move(train), std::move(test));
}

ListProblem::ListProblem(Task task, std::vector<std::vector<double>> train, std::vector<std::vector<double>> test)
    : task_(task), train_(std::move(train)), test_(std::move(test)) {}

std::string ListProblem::name() const { return std::string("list:") + to_string(task_); }

std::string ListProblem::description() const {
    return std::string("Compute the ") + to_string(task_) +
           " of a list of numbers; tested on lists up to 2.5x longer than in training";
}

std::vector<Channel> ListProblem::channels() const {
    return {{"list.length", ChannelKind::Sensor, 0, "number of items in the list", "items"},
            {"list.item", ChannelKind::Sensor, 1, "item at index i (0-based); 0 outside the list", ""}};
}

double ListProblem::answer(const std::vector<double>& l) const {
    switch (task_) {
        case Task::Sum: {
            double s = 0;
            for (double v : l) s += v;
            return s;
        }
        case Task::Max: return *std::max_element(l.begin(), l.end());
        case Task::CountPositive: return static_cast<double>(std::count_if(l.begin(), l.end(), [](double v) { return v > 0; }));
    }
    return 0;
}

namespace {
struct ListEnv : Environment {
    const std::vector<double>& l;
    explicit ListEnv(const std::vector<double>& list) : l(list) {}
    double sense(int channel, const double* args, int) override {
        if (channel == 0) return static_cast<double>(l.size());
        const double i = std::floor(args[0]);
        return i >= 0 && i < static_cast<double>(l.size()) ? l[static_cast<std::size_t>(i)] : 0.0;
    }
};
}  // namespace

double ListProblem::predict(Runner& runner, const Program& program, const std::vector<double>& list) const {
    std::array<double, 256> regs{};
    ListEnv env(list);
    runner.run(program, env, std::span<double>(regs.data(), static_cast<std::size_t>(runner.spec().num_registers)));
    return regs[0];
}

double ListProblem::evaluate_case(Runner& runner, const Program& program, Split split, std::size_t i) const {
    const auto& l = lists(split)[i];
    const double err = std::fabs(answer(l) - predict(runner, program, l));
    return std::isfinite(err) ? -std::min(err, 1e6) : -1e6;
}

std::vector<Metric> ListProblem::metrics(const MachineSpec& spec, const Program& program, Split split) const {
    Runner runner(spec);
    const auto& ls = lists(split);
    double correct = 0, abs = 0;
    for (const auto& l : ls) {
        const double e = std::fabs(answer(l) - predict(runner, program, l));
        correct += e < 1e-6;
        abs += std::isfinite(e) ? e : 1e6;
    }
    const double n = static_cast<double>(std::max<std::size_t>(1, ls.size()));
    return {{"accuracy", correct / n}, {"mae", abs / n}};
}

bool ListProblem::solved(const MachineSpec& spec, const Program& program, Split split) const {
    return metrics(spec, program, split)[0].value >= 1.0;
}

}  // namespace adaptogen
