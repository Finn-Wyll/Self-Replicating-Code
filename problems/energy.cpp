// adaptogen-problem: energy: Control home heating over a day with changing electricity prices: stay comfortable when home, pay as little as possible (evolves a thermostat program)
//
// Every 15 minutes the evolved program sees the indoor and outdoor temperature, the price now and
// in the next hour, and whether anyone is home (or how soon they will be); it outputs a heating
// power between 0 and 1 in r0. Registers persist through the day, so the program can keep state.
// The house cools towards the outdoor temperature. Score: electricity cost plus a penalty for
// every degree-hour outside the comfort band while someone is home. A standard thermostat
// (heat whenever someone is home and it is below the band's midpoint) is reported for comparison.
#include <adaptogen/sdk.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace adaptogen::sdk;

class Energy : public Problem {
public:
    Energy(const Params& p, std::uint64_t seed) {
        Rng rng(seed);
        auto make = [&](int n_cases, std::vector<Day>& out) {
            for (int c = 0; c < n_cases; ++c) {
                Day d;
                const double mean_out = rng.uniform(-5, 12), swing = rng.uniform(2, 8);
                const int leave = rng.range(26, 36), back = rng.range(64, 76);  // 6:30-9:00, 16:00-19:00
                const bool home_all_day = rng.chance(0.2);
                for (int t = 0; t < kSteps; ++t) {
                    const double hour = t / 4.0;
                    d.outdoor.push_back(mean_out - swing * std::cos((hour - 4) / 24.0 * 6.283185307) + rng.uniform(-0.5, 0.5));
                    double price = 0.18;
                    if ((hour >= 7 && hour < 9) || (hour >= 17 && hour < 21)) price = 0.42;
                    else if (hour < 6 || hour >= 23) price = 0.10;
                    d.price.push_back(price * rng.uniform(0.85, 1.15));
                    d.home.push_back(home_all_day || t < leave || t >= back);
                }
                d.start = rng.uniform(17, 21);
                out.push_back(std::move(d));
            }
        };
        make(p.get("train_cases", 40), train_);
        make(p.get("test_cases", 100), test_);
    }

    std::string name() const override { return "energy"; }
    std::string description() const override {
        return "Heat a home over a day: comfortable when someone is home, as cheap as possible";
    }
    std::vector<Channel> channels() const override {
        return {sensor("temp.inside", 0, "indoor temperature", "C"),
                sensor("temp.outside", 0, "outdoor temperature", "C"),
                sensor("price.now", 0, "electricity price now", "EUR/kWh"),
                sensor("price.next_hour", 0, "average electricity price over the next hour", "EUR/kWh"),
                sensor("home.now", 0, "1 if someone is home, else 0"),
                sensor("home.in", 0, "hours until someone is home again (0 if home now)", "hours"),
                sensor("comfort.min", 0, "lowest comfortable temperature", "C"),
                sensor("comfort.max", 0, "highest comfortable temperature", "C"),
                sensor("time.hour", 0, "hour of the day (0..24)", "hours")};
    }
    int num_outputs() const override { return 1; }
    std::size_t num_cases(Split s) const override { return days(s).size(); }

    double evaluate_case(Runner& run, Split s, std::size_t i) const override {
        const Day& d = days(s)[i];
        return -simulate(d, [&](int t, double inside) {
            auto env = make_env([&](int ch, const double*, int) { return sense(d, t, inside, ch); });
            run(env);  // registers persist between steps: the program may remember things
            const double v = run.reg(0);
            return std::isfinite(v) ? std::clamp(v, 0.0, 1.0) : 0.0;
        }).total();
    }

    std::vector<Metric> metrics(const Evaluator& eval, Split s) const override {
        double mine = 0, thermo = 0, tcost = 0, tdis = 0;
        const std::size_t n = num_cases(s);
        for (std::size_t i = 0; i < n; ++i) {
            const Day& d = days(s)[i];
            mine += -eval.score(s, i);
            const Outcome o = simulate(d, [&](int t, double inside) { return d.home[static_cast<std::size_t>(t)] && inside < 21.5 ? 1.0 : 0.0; });
            thermo += o.total();
            tcost += o.cost;
            tdis += o.discomfort;
        }
        const double k = static_cast<double>(std::max<std::size_t>(1, n));
        return {{"cost_plus_discomfort", mine / k},
                {"thermostat_cost_plus_discomfort", thermo / k},
                {"thermostat_cost_eur", tcost / k},
                {"thermostat_discomfort_degree_hours", tdis / k}};
    }

private:
    static constexpr int kSteps = 96;  // 15-minute steps
    struct Day {
        std::vector<double> outdoor, price;
        std::vector<bool> home;
        double start = 19;
    };
    struct Outcome {
        double cost = 0, discomfort = 0;
        double total() const { return cost + 0.5 * discomfort; }  // 1 degree-hour of discomfort = 0.50 EUR
    };
    static double sense(const Day& d, int t, double inside, int ch) {
        const std::size_t u = static_cast<std::size_t>(t);
        switch (ch) {
            case 0: return inside;
            case 1: return d.outdoor[u];
            case 2: return d.price[u];
            case 3: {
                double s = 0;
                for (int k = 1; k <= 4; ++k) s += d.price[static_cast<std::size_t>(std::min(kSteps - 1, t + k))];
                return s / 4;
            }
            case 4: return d.home[u] ? 1.0 : 0.0;
            case 5: {
                int k = t;
                while (k < kSteps && !d.home[static_cast<std::size_t>(k)]) ++k;
                return (k - t) / 4.0;
            }
            case 6: return 20.0;
            case 7: return 23.0;
            case 8: return t / 4.0;
        }
        return 0.0;
    }
    template <class Control>
    static Outcome simulate(const Day& d, const Control& control) {
        Outcome o;
        double inside = d.start;
        for (int t = 0; t < kSteps; ++t) {
            const double power = control(t, inside);         // 0..1 of a 4 kW heater
            o.cost += power * 4.0 * 0.25 * d.price[static_cast<std::size_t>(t)];
            inside += 0.06 * (d.outdoor[static_cast<std::size_t>(t)] - inside) + 1.0 * power;
            if (d.home[static_cast<std::size_t>(t)]) {
                if (inside < 20) o.discomfort += (20 - inside) * 0.25;
                if (inside > 23) o.discomfort += (inside - 23) * 0.25;
            }
        }
        return o;
    }
    const std::vector<Day>& days(Split s) const { return s == Split::Train ? train_ : test_; }
    std::vector<Day> train_, test_;
};

ADAPTOGEN_PLUGIN(Energy)
