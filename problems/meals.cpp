// adaptogen-problem: meals: Plan a day's food that meets calorie and protein targets at the lowest cost (evolves a shopping rule)
//
// Each step, the evolved rule scores every food and one serving of the best is added, until both
// targets are met (or 25 servings). Cost is compared with a simple rule ("most calories per unit
// of money"). Missing a target is penalised heavily.
#include "rules.hpp"

#include <algorithm>

using namespace adaptogen::sdk;

class Meals : public Problem {
public:
    Meals(const Params& p, std::uint64_t seed) {
        Rng rng(seed);
        auto make = [&](int n_cases, std::vector<Case>& out) {
            for (int c = 0; c < n_cases; ++c) {
                Case k;
                const int n = rng.range(6, 12);
                for (int i = 0; i < n; ++i) {
                    Food f;
                    f.calories = rng.range(50, 700);
                    f.protein = std::round(rng.uniform(0, 1) * rng.uniform(0, 45));
                    // price loosely follows nutrition, with lots of noise (bargains exist)
                    f.cost = std::round((0.2 + f.calories / 400.0 + f.protein / 15.0) * rng.uniform(0.4, 1.6) * 20) / 20;
                    k.foods.push_back(f);
                }
                k.calories = rng.range(1800, 2600);
                k.protein = rng.range(50, 110);
                k.reference = std::max(0.01, plan(k, [&](const std::vector<int>& all, double, double) {
                                                  return *std::max_element(all.begin(), all.end(), [&](int a, int b) {
                                                      return k.foods[a].calories / k.foods[a].cost < k.foods[b].calories / k.foods[b].cost;
                                                  });
                                              }).cost);
                out.push_back(std::move(k));
            }
        };
        make(p.get("train_cases", 60), train_);
        make(p.get("test_cases", 150), test_);
    }

    std::string name() const override { return "meals"; }
    std::string description() const override { return "Choose servings of foods to meet calorie and protein targets at minimum cost"; }
    std::vector<Channel> channels() const override {
        return {sensor("item.cost", 0, "price of one serving of this food", "EUR"),
                sensor("item.calories", 0, "calories in one serving", "kcal"),
                sensor("item.protein", 0, "protein in one serving", "g"),
                sensor("need.calories", 0, "calories still needed today (0 if met)", "kcal"),
                sensor("need.protein", 0, "protein still needed today (0 if met)", "g"),
                sensor("servings.left", 0, "servings that may still be added", "servings")};
    }
    std::size_t num_cases(Split s) const override { return cases(s).size(); }

    double evaluate_case(Runner& run, Split s, std::size_t i) const override {
        const Case& c = cases(s)[i];
        return score(c, plan(c, [&](const std::vector<int>& all, double need_cal, double need_pro) {
                         return all[static_cast<std::size_t>(rules::pick(run, static_cast<int>(all.size()), [&](int k, int ch, const double*) {
                             const Food& f = c.foods[static_cast<std::size_t>(k)];
                             switch (ch) {
                                 case 0: return f.cost;
                                 case 1: return f.calories;
                                 case 2: return f.protein;
                                 case 3: return need_cal;
                                 case 4: return need_pro;
                                 case 5: return static_cast<double>(kMaxServings);
                             }
                             return 0.0;
                         }))];
                     }));
    }

    std::vector<Metric> metrics(const Evaluator& eval, Split s) const override {
        double met = 0, ratio = 0;
        const std::size_t n = num_cases(s);
        for (std::size_t i = 0; i < n; ++i) {
            const double sc = eval.score(s, i);
            if (sc > -kMissPenalty) {  // targets met: the score is minus the cost ratio
                met += 1;
                ratio += -sc;
            }
        }
        return {{"cost_vs_simple_rule", met > 0 ? ratio / met : 0.0},
                {"targets_met", met / static_cast<double>(std::max<std::size_t>(1, n))}};
    }

private:
    static constexpr int kMaxServings = 25;
    struct Food {
        double cost = 0, calories = 0, protein = 0;
    };
    struct Case {
        std::vector<Food> foods;
        double calories = 0, protein = 0, reference = 1;
    };
    struct Plan {
        double cost = 0, short_cal = 0, short_pro = 0;
    };
    template <class Choose>
    static Plan plan(const Case& c, const Choose& choose) {
        std::vector<int> all(c.foods.size());
        for (std::size_t k = 0; k < all.size(); ++k) all[k] = static_cast<int>(k);
        double cal = 0, pro = 0, cost = 0;
        for (int s = 0; s < kMaxServings && (cal < c.calories || pro < c.protein); ++s) {
            const Food& f = c.foods[static_cast<std::size_t>(choose(all, std::max(0.0, c.calories - cal), std::max(0.0, c.protein - pro)))];
            cal += f.calories;
            pro += f.protein;
            cost += f.cost;
        }
        return {cost, std::max(0.0, c.calories - cal) / c.calories, std::max(0.0, c.protein - pro) / c.protein};
    }
    // Lower cost relative to the simple rule is better. Missing a target costs far more than any
    // price difference, with partial credit for getting closer.
    static constexpr double kMissPenalty = 100.0;
    static double score(const Case& c, const Plan& p) {
        const double miss = p.short_cal + p.short_pro;
        return -(p.cost / c.reference) - (miss > 0 ? kMissPenalty + 100.0 * miss : 0.0);
    }
    const std::vector<Case>& cases(Split s) const { return s == Split::Train ? train_ : test_; }
    std::vector<Case> train_, test_;
};

ADAPTOGEN_PLUGIN(Meals)
