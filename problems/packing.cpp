// adaptogen-problem: packing: Pack a bag with a weight limit for the most total value (evolves a packing rule; scored against the true optimum)
//
// Items have a weight and a value; the bag has a weight limit. At each step the evolved rule
// scores every item that still fits and the best one goes in, until nothing fits. The score is
// the packed value as a fraction of the best possible packing (computed exactly).
#include "rules.hpp"

#include <algorithm>

using namespace adaptogen::sdk;

class Packing : public Problem {
public:
    Packing(const Params& p, std::uint64_t seed) {
        Rng rng(seed);
        const int lo = p.get("min_items", 10), hi = p.get("max_items", 18);
        auto make = [&](int n_cases, std::vector<Case>& out) {
            for (int c = 0; c < n_cases; ++c) {
                Case k;
                const int n = rng.range(lo, hi);
                int total = 0;
                for (int i = 0; i < n; ++i) {
                    k.weight.push_back(rng.range(1, 20));
                    k.value.push_back(rng.range(1, 30));
                    total += k.weight.back();
                }
                k.capacity = std::max(1, static_cast<int>(rng.uniform(0.25, 0.5) * total));
                k.optimum = optimum(k);
                out.push_back(std::move(k));
            }
        };
        make(p.get("train_cases", 60), train_);
        make(p.get("test_cases", 150), test_);
    }

    std::string name() const override { return "packing"; }
    std::string description() const override { return "Choose items under a weight limit to maximise total value"; }
    std::vector<Channel> channels() const override {
        return {sensor("item.weight", 0, "weight of this item", "kg"),
                sensor("item.value", 0, "how much this item is worth to you"),
                sensor("capacity.left", 0, "weight that still fits in the bag", "kg"),
                sensor("items.remaining", 0, "items not packed yet", "items")};
    }
    std::size_t num_cases(Split s) const override { return cases(s).size(); }

    double evaluate_case(Runner& run, Split s, std::size_t i) const override {
        const Case& c = cases(s)[i];
        const int v = pack(c, [&](const std::vector<int>& fits, int left, int remaining) {
            return fits[static_cast<std::size_t>(rules::pick(run, static_cast<int>(fits.size()), [&](int k, int ch, const double*) {
                const int it = fits[static_cast<std::size_t>(k)];
                switch (ch) {
                    case 0: return static_cast<double>(c.weight[static_cast<std::size_t>(it)]);
                    case 1: return static_cast<double>(c.value[static_cast<std::size_t>(it)]);
                    case 2: return static_cast<double>(left);
                    case 3: return static_cast<double>(remaining);
                }
                return 0.0;
            }))];
        });
        return static_cast<double>(v) / static_cast<double>(std::max(1, c.optimum));
    }

    std::vector<Metric> metrics(const Evaluator& eval, Split s) const override {
        std::vector<double> mine, ratio;
        double optimal = 0;
        for (std::size_t i = 0; i < num_cases(s); ++i) {
            const Case& c = cases(s)[i];
            const double f = eval.score(s, i);
            mine.push_back(f);
            optimal += f >= 1.0 - 1e-12;
            ratio.push_back(static_cast<double>(pack(c, [&](const std::vector<int>& fits, int, int) {
                                return *std::max_element(fits.begin(), fits.end(), [&](int a, int b) {
                                    return static_cast<double>(c.value[a]) / c.weight[a] < static_cast<double>(c.value[b]) / c.weight[b];
                                });
                            })) / std::max(1, c.optimum));
        }
        return {{"fraction_of_optimum", rules::mean(mine)},
                {"optimal_packings", optimal / static_cast<double>(std::max<std::size_t>(1, num_cases(s)))},
                {"value_per_kg_rule_fraction", rules::mean(ratio)}};
    }
    bool solved(const Evaluator& eval, Split s) const override { return metrics(eval, s)[1].value >= 1.0; }

private:
    struct Case {
        std::vector<int> weight, value;
        int capacity = 0, optimum = 0;
    };
    template <class Choose>
    static int pack(const Case& c, const Choose& choose) {
        std::vector<int> left(c.weight.size());
        for (std::size_t k = 0; k < left.size(); ++k) left[k] = static_cast<int>(k);
        int cap = c.capacity, total = 0;
        for (;;) {
            std::vector<int> fits;
            for (int k : left)
                if (c.weight[static_cast<std::size_t>(k)] <= cap) fits.push_back(k);
            if (fits.empty()) break;
            const int k = choose(fits, cap, static_cast<int>(left.size()));
            cap -= c.weight[static_cast<std::size_t>(k)];
            total += c.value[static_cast<std::size_t>(k)];
            left.erase(std::find(left.begin(), left.end(), k));
        }
        return total;
    }
    static int optimum(const Case& c) {  // 0/1 knapsack, exact
        std::vector<int> best(static_cast<std::size_t>(c.capacity) + 1, 0);
        for (std::size_t i = 0; i < c.weight.size(); ++i)
            for (int w = c.capacity; w >= c.weight[i]; --w)
                best[static_cast<std::size_t>(w)] = std::max(best[static_cast<std::size_t>(w)], best[static_cast<std::size_t>(w - c.weight[i])] + c.value[i]);
        return best[static_cast<std::size_t>(c.capacity)];
    }
    const std::vector<Case>& cases(Split s) const { return s == Split::Train ? train_ : test_; }
    std::vector<Case> train_, test_;
};

ADAPTOGEN_PLUGIN(Packing)
