#pragma once
// Formula discovery (symbolic regression): find a program that reproduces a hidden
// function from examples. Sensors: one per input variable. Output: register r0.
#include <functional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "adaptogen/problem.hpp"
#include "adaptogen/random.hpp"

namespace adaptogen {

struct Dataset {
    std::vector<std::string> variables;
    std::vector<std::vector<double>> x;  // rows
    std::vector<double> y;
    std::size_t size() const { return y.size(); }
};

struct RegressionMetrics {
    double mae = 0, rmse = 0, r2 = 0, nrmse = 0;
};
RegressionMetrics regression_metrics(const std::vector<double>& y, const std::vector<double>& pred);

/// Standard GP benchmark targets (Koza, Nguyen, Keijzer, Pagie).
struct FormulaTarget {
    std::string name;
    std::string formula;
    std::function<Dataset(Rng&)> train;
    std::function<Dataset(Rng&)> test;
};
const std::vector<FormulaTarget>& formula_targets();

/// Load a CSV with a header row. The target column is `target` (default: last column).
Dataset load_csv(const std::string& path, const std::string& target = "");

class FormulaProblem : public Problem {
public:
    FormulaProblem(std::string name, std::string description, Dataset train, Dataset test);
    /// params: {"target": "koza1"} or {"data": "file.csv", "target_column": "y", "test_fraction": 0.25}
    static std::unique_ptr<FormulaProblem> from_params(const nlohmann::json& params, std::uint64_t seed);

    std::string name() const override { return name_; }
    std::string description() const override { return description_; }
    std::vector<Channel> channels() const override;
    std::vector<Op> default_ops() const override { return core_ops(); }
    std::size_t num_cases(Split s) const override { return data(s).size(); }
    double evaluate_case(Runner& runner, const Program& program, Split split, std::size_t index) const override;
    std::vector<Metric> metrics(const MachineSpec& spec, const Program& program, Split split) const override;
    bool solved(const MachineSpec& spec, const Program& program, Split split) const override;

    double predict(Runner& runner, const Program& program, const std::vector<double>& x) const;
    const Dataset& data(Split s) const { return s == Split::Train ? train_ : test_; }

    static constexpr double kSolvedNrmse = 0.01;  // RMSE below 1% of the target's spread

private:
    std::string name_, description_;
    Dataset train_, test_;
};

}  // namespace adaptogen
