// adaptogen-problem: foraging: Steer an agent around a grid to eat all the food in as few steps as possible (evolves a controller)
//
// Each step the evolved program sees where the nearest food is and outputs a move in r0 (x) and
// r1 (y): the agent steps one cell along whichever has the larger magnitude, in its sign's
// direction. Registers persist between steps. Score: share of food eaten, plus a bonus for
// finishing early.
#include <adaptogen/sdk.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace adaptogen::sdk;

class Foraging : public Problem {
public:
    Foraging(const Params& p, std::uint64_t seed) {
        Rng rng(seed);
        size_ = p.get("size", 12);
        const int food = p.get("food", 8);
        auto make = [&](int n_cases, std::vector<World>& out) {
            for (int c = 0; c < n_cases; ++c) {
                World w;
                while (static_cast<int>(w.food.size()) < food) {
                    const Cell f{rng.range(0, size_ - 1), rng.range(0, size_ - 1)};
                    if (f.x == size_ / 2 && f.y == size_ / 2) continue;
                    if (std::find(w.food.begin(), w.food.end(), f) == w.food.end()) w.food.push_back(f);
                }
                out.push_back(std::move(w));
            }
        };
        make(p.get("train_cases", 30), train_);
        make(p.get("test_cases", 100), test_);
        steps_ = p.get("steps", 6 * size_ + 4 * food);
    }

    std::string name() const override { return "foraging"; }
    std::string description() const override { return "Steer an agent on a grid to eat all the food quickly"; }
    std::vector<Channel> channels() const override {
        return {sensor("food.dx", 0, "x offset to the nearest food (positive = right)", "cells"),
                sensor("food.dy", 0, "y offset to the nearest food (positive = down)", "cells"),
                sensor("food.distance", 0, "steps to the nearest food", "cells"),
                sensor("food.remaining", 0, "pieces of food left", "items"),
                sensor("steps.left", 0, "steps left in the episode", "steps")};
    }
    int num_outputs() const override { return 2; }
    std::size_t num_cases(Split s) const override { return worlds(s).size(); }

    double evaluate_case(Runner& run, Split s, std::size_t i) const override {
        std::vector<Cell> food = worlds(s)[i].food;
        const double total = static_cast<double>(food.size());
        Cell me{size_ / 2, size_ / 2};
        int t = 0;
        for (; t < steps_ && !food.empty(); ++t) {
            const auto near = *std::min_element(food.begin(), food.end(), [&](const Cell& a, const Cell& b) { return dist(me, a) < dist(me, b); });
            auto env = make_env([&](int ch, const double*, int) {
                switch (ch) {
                    case 0: return static_cast<double>(near.x - me.x);
                    case 1: return static_cast<double>(near.y - me.y);
                    case 2: return static_cast<double>(dist(me, near));
                    case 3: return static_cast<double>(food.size());
                    case 4: return static_cast<double>(steps_ - t);
                }
                return 0.0;
            });
            run(env);
            const double dx = run.reg(0), dy = run.reg(1);
            if (std::isfinite(dx) && std::isfinite(dy) && (dx != 0 || dy != 0)) {
                if (std::fabs(dx) >= std::fabs(dy)) me.x = std::clamp(me.x + (dx > 0 ? 1 : -1), 0, size_ - 1);
                else me.y = std::clamp(me.y + (dy > 0 ? 1 : -1), 0, size_ - 1);
            }
            food.erase(std::remove(food.begin(), food.end(), me), food.end());
        }
        const double eaten = (total - static_cast<double>(food.size())) / total;
        return eaten + (food.empty() ? 0.5 * (1.0 - static_cast<double>(t) / steps_) : 0.0);
    }

    std::vector<Metric> metrics(const Evaluator& eval, Split s) const override {
        double eaten = 0, cleared = 0;
        const std::size_t n = num_cases(s);
        for (std::size_t i = 0; i < n; ++i) {
            const double v = eval.score(s, i);
            eaten += std::min(1.0, v);
            cleared += v >= 1.0;
        }
        const double k = static_cast<double>(std::max<std::size_t>(1, n));
        return {{"food_eaten", eaten / k}, {"all_food_eaten", cleared / k}};
    }
    bool solved(const Evaluator& eval, Split s) const override { return metrics(eval, s)[1].value >= 1.0; }

private:
    struct Cell {
        int x = 0, y = 0;
        bool operator==(const Cell&) const = default;
    };
    struct World {
        std::vector<Cell> food;
    };
    static int dist(const Cell& a, const Cell& b) { return std::abs(a.x - b.x) + std::abs(a.y - b.y); }
    const std::vector<World>& worlds(Split s) const { return s == Split::Train ? train_ : test_; }
    std::vector<World> train_, test_;
    int size_ = 12, steps_ = 100;
};

ADAPTOGEN_PLUGIN(Foraging)
