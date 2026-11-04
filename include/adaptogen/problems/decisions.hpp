#pragma once
// Decisions from data: learn to choose the best option from logged examples, without code.
//
// Each case is one decision between several options. Every option has numeric features and a
// reward (how good it turned out; 1/0 if you only know which one was right). The evolved program
// is a scoring rule: it scores each option from its features (plus the case's context and the
// per-case minimum/maximum of every feature, so it can compare options), and the highest score
// is chosen.
//
// Data formats:
//   CSV, one row per option:   case,price,eta,rating,context.weekday,reward[,option]
//       `case` groups rows into decisions; `reward` is the outcome; `context.*` columns describe the
//       situation (the same for all options of a case); an optional `option` column names options.
//   JSON: {"cases": [{"context": {...}, "options": [{"price": 10, "eta": 3, "reward": 1}, ...]}, ...]}
//
// To show the evolved rule is worth using, every split also reports the simple solutions a
// developer would otherwise write, fitted on the training cases: the best single-feature rule
// ("always pick the cheapest") and a linear scoring model (conditional logit).
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "adaptogen/problem.hpp"

namespace adaptogen {

struct DecisionCase {
    std::vector<std::vector<double>> options;  // options x features
    std::vector<double> rewards;
    std::vector<double> context;
    std::vector<std::string> labels;
};

struct DecisionData {
    std::vector<std::string> features, context;
    std::vector<DecisionCase> cases;
};

/// Load CSV or JSON (by extension) decision data.
DecisionData load_decision_data(const std::string& path);

class DecisionProblem : public Problem {
public:
    /// params: {"data": "file.csv" | ["a.csv", "b.csv"], "test_fraction": 0.25}
    static std::unique_ptr<DecisionProblem> from_params(const nlohmann::json& params, std::uint64_t seed);
    DecisionProblem(std::string name, DecisionData train, DecisionData test);

    std::string name() const override { return name_; }
    std::string description() const override;
    std::vector<Channel> channels() const override;
    std::size_t num_cases(Split s) const override { return data(s).cases.size(); }
    double evaluate_case(Runner& runner, const Program& program, Split split, std::size_t index) const override;
    std::vector<Metric> metrics(const MachineSpec& spec, const Program& program, Split split) const override;
    bool solved(const MachineSpec& spec, const Program& program, Split split) const override;
    nlohmann::json strain_extras(const MachineSpec& spec, const Program& program) const override;

    /// Scores the program gives each option of a case (higher = preferred).
    std::vector<double> option_scores(Runner& runner, const Program& program, const DecisionCase& c) const;
    const DecisionData& data(Split s) const { return s == Split::Train ? train_ : test_; }

private:
    double evaluate_choice(const DecisionCase& c, int choice) const;
    int baseline_single_feature(const DecisionCase& c) const;
    int baseline_linear(const DecisionCase& c) const;
    void fit_baselines();

    std::string name_;
    DecisionData train_, test_;
    // simple solutions fitted on the training cases
    int sf_feature_ = 0, sf_sign_ = 1;
    std::vector<double> lin_w_, mean_, sd_;
};

/// Shared by decide(): rank-based margin between the two best scores in [0, 1].
double decision_margin(const std::vector<double>& scores);

}  // namespace adaptogen
