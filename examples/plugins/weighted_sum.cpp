// adaptogen plug-in: weighted_sum
//
// Derived from the strain list-sum-v6 (see weighted_sum_base.hpp, generated with
// `adaptogen plugin new weighted_sum --from list-sum-v6`). It inherits the parent's channels
// list.length and list.item unchanged, and adds list.weight. The task is the weighted sum
// sum_i item[i] * weight[i]. The parent's loop over the list is reusable; evolution only has
// to bring the weights in.
//
// Build and check:  adaptogen plugin validate examples/plugins/weighted_sum.cpp --library benchmarks/strains
// Evolve:           adaptogen evolve --problem plugin:examples/plugins/weighted_sum.cpp --seed-parents --library benchmarks/strains
#include "weighted_sum_base.hpp"

#include <adaptogen/sdk.hpp>

using namespace adaptogen::sdk;

class WeightedSum : public WeightedSumBase {
public:
    WeightedSum(const Params& params, std::uint64_t seed) {
        Rng rng(seed);
        auto make = [&](int cases, int max_len, std::vector<Case>& out) {
            for (int i = 0; i < cases; ++i) {
                Case c;
                const int n = rng.range(1, max_len);
                for (int k = 0; k < n; ++k) {
                    c.items.push_back(rng.range(-9, 9));
                    c.weights.push_back(rng.range(0, 5));
                }
                out.push_back(std::move(c));
            }
        };
        // Like the parent problem: test lists are longer than training lists.
        make(params.get("train_cases", 100), params.get("train_max_length", 20), train_);
        make(params.get("test_cases", 200), params.get("test_max_length", 50), test_);
    }

    std::string name() const override { return "weighted_sum"; }
    std::string description() const override {
        return "Weighted sum of a list (sum of item * weight); tested on lists up to 2.5x longer than in training";
    }

    static constexpr int list_weight = kInheritedChannels;  // our first extra channel
    std::vector<Channel> extra_channels() const override {
        return {sensor("list.weight", 1, "weight of the item at index i (0-based); 0 outside the list")};
    }

    std::size_t num_cases(Split s) const override { return cases(s).size(); }

    double evaluate_case(Runner& run, Split s, std::size_t i) const override {
        const Case& c = cases(s)[i];
        auto at = [](const std::vector<double>& v, double idx) {
            const double k = std::floor(idx);
            return k >= 0 && k < static_cast<double>(v.size()) ? v[static_cast<std::size_t>(k)] : 0.0;
        };
        auto env = make_env([&](int channel, const double* args, int) {
            switch (channel) {
                case list_length: return static_cast<double>(c.items.size());
                case list_item: return at(c.items, args[0]);
                case list_weight: return at(c.weights, args[0]);
            }
            return 0.0;
        });
        run(env);
        const double err = std::fabs(run.reg(0) - answer(c));
        return std::isfinite(err) ? -std::min(err, 1e6) : -1e6;
    }

    std::vector<Metric> metrics(const Evaluator& eval, Split s) const override {
        double correct = 0, abs = 0;
        const std::size_t n = num_cases(s);
        for (std::size_t i = 0; i < n; ++i) {
            const double e = -eval.score(s, i);
            correct += e < 1e-6;
            abs += e;
        }
        return {{"accuracy", n ? correct / static_cast<double>(n) : 0.0}, {"mae", n ? abs / static_cast<double>(n) : 0.0}};
    }
    bool solved(const Evaluator& eval, Split s) const override { return metrics(eval, s)[0].value >= 1.0; }

private:
    struct Case {
        std::vector<double> items, weights;
    };
    static double answer(const Case& c) {
        double s = 0;
        for (std::size_t k = 0; k < c.items.size(); ++k) s += c.items[k] * c.weights[k];
        return s;
    }
    const std::vector<Case>& cases(Split s) const { return s == Split::Train ? train_ : test_; }
    std::vector<Case> train_, test_;
};

ADAPTOGEN_PLUGIN(WeightedSum)
