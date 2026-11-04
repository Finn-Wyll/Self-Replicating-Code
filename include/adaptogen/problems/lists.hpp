#pragma once
// List algorithms: compute a property of a list of numbers (sum, max, count of positives).
// Programs must discover iteration themselves. Training lists are short; test lists are
// up to 4x longer, so only programs that really loop over the list generalise.
#include <nlohmann/json.hpp>

#include "adaptogen/problem.hpp"
#include "adaptogen/random.hpp"

namespace adaptogen {

class ListProblem : public Problem {
public:
    enum class Task { Sum, Max, CountPositive };

    /// params: {"task": "sum"|"max"|"count_positive", "train_cases": 60, "train_max_length": 10,
    ///          "test_cases": 200, "test_max_length": 40, "value_range": 9}
    static std::unique_ptr<ListProblem> from_params(const nlohmann::json& params, std::uint64_t seed);
    ListProblem(Task task, std::vector<std::vector<double>> train, std::vector<std::vector<double>> test);

    std::string name() const override;
    std::string description() const override;
    std::vector<Channel> channels() const override;
    std::size_t num_cases(Split s) const override { return lists(s).size(); }
    double evaluate_case(Runner& runner, const Program& program, Split split, std::size_t index) const override;
    std::vector<Metric> metrics(const MachineSpec& spec, const Program& program, Split split) const override;
    bool solved(const MachineSpec& spec, const Program& program, Split split) const override;

    double answer(const std::vector<double>& list) const;
    double predict(Runner& runner, const Program& program, const std::vector<double>& list) const;
    const std::vector<std::vector<double>>& lists(Split s) const { return s == Split::Train ? train_ : test_; }

private:
    Task task_;
    std::vector<std::vector<double>> train_, test_;
};

const char* to_string(ListProblem::Task t);

}  // namespace adaptogen
