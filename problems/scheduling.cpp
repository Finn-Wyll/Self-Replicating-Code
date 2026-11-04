// adaptogen-problem: scheduling: Order tasks with durations, deadlines and priorities to minimise weighted lateness (evolves a dispatching rule)
//
// One worker, N tasks. At each step the evolved rule scores every remaining task and the
// highest-scoring one is done next. Lateness = how far past its deadline a task finishes,
// weighted by its priority. Classic hand-made rules (earliest deadline first, shortest weighted
// processing time) are reported for comparison.
#include "rules.hpp"

#include <algorithm>
#include <numeric>

using namespace adaptogen::sdk;

class Scheduling : public Problem {
public:
    Scheduling(const Params& p, std::uint64_t seed) {
        Rng rng(seed);
        const int lo = p.get("min_tasks", 6), hi = p.get("max_tasks", 12);
        auto make = [&](int n_cases, std::vector<Case>& out) {
            for (int c = 0; c < n_cases; ++c) {
                Case k;
                const int n = rng.range(lo, hi);
                double total = 0;
                for (int i = 0; i < n; ++i) {
                    Task t;
                    t.duration = rng.range(1, 10);
                    t.priority = rng.range(1, 5);
                    total += t.duration;
                    k.tasks.push_back(t);
                }
                for (auto& t : k.tasks) t.deadline = std::round(rng.uniform(t.duration, 0.8 * total));
                out.push_back(std::move(k));
            }
        };
        make(p.get("train_cases", 60), train_);
        make(p.get("test_cases", 150), test_);
    }

    std::string name() const override { return "scheduling"; }
    std::string description() const override {
        return "Order tasks (duration, deadline, priority) to minimise priority-weighted lateness";
    }
    std::vector<Channel> channels() const override {
        return {sensor("item.duration", 0, "how long this task takes", "hours"),
                sensor("item.deadline", 0, "when this task is due, counted from the start", "hours"),
                sensor("item.priority", 0, "importance of this task (1 = low, 5 = high)"),
                sensor("time.now", 0, "time already spent", "hours"),
                sensor("items.remaining", 0, "number of tasks not yet done", "items")};
    }
    std::size_t num_cases(Split s) const override { return cases(s).size(); }

    double evaluate_case(Runner& run, Split s, std::size_t i) const override {
        const Case& c = cases(s)[i];
        return -lateness_of(c, [&](const std::vector<int>& left, double now) {
                   return left[static_cast<std::size_t>(rules::pick(run, static_cast<int>(left.size()), [&](int k, int ch, const double*) {
                       const Task& t = c.tasks[static_cast<std::size_t>(left[static_cast<std::size_t>(k)])];
                       switch (ch) {
                           case 0: return t.duration;
                           case 1: return t.deadline;
                           case 2: return t.priority;
                           case 3: return now;
                           case 4: return static_cast<double>(left.size());
                       }
                       return 0.0;
                   }))];
               }) / scale(c);
    }

    std::vector<Metric> metrics(const Evaluator& eval, Split s) const override {
        std::vector<double> mine, edd, wspt;
        for (std::size_t i = 0; i < num_cases(s); ++i) {
            const Case& c = cases(s)[i];
            mine.push_back(-eval.score(s, i) * scale(c));
            edd.push_back(lateness_of(c, [&](const std::vector<int>& left, double) {
                return *std::min_element(left.begin(), left.end(), [&](int a, int b) { return c.tasks[a].deadline < c.tasks[b].deadline; });
            }));
            wspt.push_back(lateness_of(c, [&](const std::vector<int>& left, double) {
                return *std::max_element(left.begin(), left.end(), [&](int a, int b) {
                    return c.tasks[a].priority / c.tasks[a].duration < c.tasks[b].priority / c.tasks[b].duration;
                });
            }));
        }
        return {{"weighted_lateness", rules::mean(mine)},
                {"edd_weighted_lateness", rules::mean(edd)},
                {"wspt_weighted_lateness", rules::mean(wspt)},
                {"better_than_best_classic_rule", rules::mean(mine) < std::min(rules::mean(edd), rules::mean(wspt)) ? 1.0 : 0.0}};
    }

private:
    struct Task {
        double duration = 0, deadline = 0, priority = 0;
    };
    struct Case {
        std::vector<Task> tasks;
    };
    template <class Choose>
    static double lateness_of(const Case& c, const Choose& choose) {
        std::vector<int> left(c.tasks.size());
        std::iota(left.begin(), left.end(), 0);
        double now = 0, cost = 0;
        while (!left.empty()) {
            const int t = choose(left, now);
            const Task& task = c.tasks[static_cast<std::size_t>(t)];
            now += task.duration;
            cost += task.priority * std::max(0.0, now - task.deadline);
            left.erase(std::find(left.begin(), left.end(), t));
        }
        return cost;
    }
    static double scale(const Case& c) {
        double s = 0;
        for (const auto& t : c.tasks) s += t.priority * t.duration;
        return std::max(1.0, s);
    }
    const std::vector<Case>& cases(Split s) const { return s == Split::Train ? train_ : test_; }
    std::vector<Case> train_, test_;
};

ADAPTOGEN_PLUGIN(Scheduling)
