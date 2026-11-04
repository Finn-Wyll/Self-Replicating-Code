// adaptogen-problem: errands: Plan a day of errands at shops with opening hours to get the most done in the least time (evolves a routing rule)
//
// You start at home with a list of shops to visit, each open only part of the day and needing
// some minutes inside. At each step the evolved rule scores every shop that can still be reached
// before it closes; the best one is visited next (waiting outside if it is not open yet).
// Reported for comparison: "nearest open shop first" and "earliest closing first".
#include "rules.hpp"

#include <algorithm>

using namespace adaptogen::sdk;

class Errands : public Problem {
public:
    Errands(const Params& p, std::uint64_t seed) {
        Rng rng(seed);
        const int lo = p.get("min_shops", 5), hi = p.get("max_shops", 9);
        auto make = [&](int n_cases, std::vector<Case>& out) {
            for (int c = 0; c < n_cases; ++c) {
                Case k;
                const int n = rng.range(lo, hi);
                for (int i = 0; i < n; ++i) {
                    Shop s;
                    s.x = rng.uniform(0, 10);
                    s.y = rng.uniform(0, 10);
                    s.open = std::round(rng.uniform(0, 240) / 15) * 15;            // opens 9:00..13:00
                    s.close = s.open + std::round(rng.uniform(90, 300) / 15) * 15;  // open 1.5..5 h
                    s.duration = rng.range(5, 25);
                    k.shops.push_back(s);
                }
                out.push_back(std::move(k));
            }
        };
        make(p.get("train_cases", 60), train_);
        make(p.get("test_cases", 150), test_);
    }

    std::string name() const override { return "errands"; }
    std::string description() const override {
        return "Visit shops with opening hours: as many errands as possible, finishing as early as possible";
    }
    std::vector<Channel> channels() const override {
        return {sensor("item.distance", 0, "distance from where you are to this shop", "km"),
                sensor("item.travel_time", 0, "minutes to get to this shop", "minutes"),
                sensor("item.opens_in", 0, "minutes until this shop opens when you would arrive (0 if open)", "minutes"),
                sensor("item.closes_in", 0, "minutes this shop stays open after you would arrive", "minutes"),
                sensor("item.duration", 0, "minutes needed inside this shop", "minutes"),
                sensor("time.now", 0, "minutes since 9:00", "minutes"),
                sensor("items.remaining", 0, "errands still to do", "items")};
    }
    std::size_t num_cases(Split s) const override { return cases(s).size(); }

    double evaluate_case(Runner& run, Split s, std::size_t i) const override {
        const Case& c = cases(s)[i];
        const Outcome o = simulate(c, [&](const std::vector<int>& ok, const State& st) {
            return ok[static_cast<std::size_t>(rules::pick(run, static_cast<int>(ok.size()), [&](int k, int ch, const double*) {
                const Shop& sh = c.shops[static_cast<std::size_t>(ok[static_cast<std::size_t>(k)])];
                const double d = dist(st, sh), arrive = st.now + d / kSpeed;
                switch (ch) {
                    case 0: return d;
                    case 1: return d / kSpeed;
                    case 2: return std::max(0.0, sh.open - arrive);
                    case 3: return sh.close - arrive;
                    case 4: return sh.duration;
                    case 5: return st.now;
                    case 6: return static_cast<double>(st.remaining);
                }
                return 0.0;
            }))];
        });
        return score(c, o);
    }

    std::vector<Metric> metrics(const Evaluator& eval, Split s) const override {
        std::vector<double> mine, nearest, closing;
        for (std::size_t i = 0; i < num_cases(s); ++i) {
            const Case& c = cases(s)[i];
            mine.push_back(eval.score(s, i));
            const Outcome a = simulate(c, [&](const std::vector<int>& ok, const State& st) {
                return *std::min_element(ok.begin(), ok.end(), [&](int x, int y) { return dist(st, c.shops[x]) < dist(st, c.shops[y]); });
            });
            const Outcome b = simulate(c, [&](const std::vector<int>& ok, const State&) {
                return *std::min_element(ok.begin(), ok.end(), [&](int x, int y) { return c.shops[x].close < c.shops[y].close; });
            });
            nearest.push_back(score(c, a));
            closing.push_back(score(c, b));
        }
        return {{"score", rules::mean(mine)},
                {"nearest_first_score", rules::mean(nearest)},
                {"earliest_closing_first_score", rules::mean(closing)}};
    }

private:
    static constexpr double kSpeed = 0.5;  // km per minute (30 km/h)
    struct Shop {
        double x = 0, y = 0, open = 0, close = 0, duration = 0;
    };
    struct Case {
        std::vector<Shop> shops;
    };
    struct State {
        double x = 5, y = 5, now = 0;
        int remaining = 0;
    };
    struct Outcome {
        int done = 0;
        double end = 0;
    };
    static double dist(const State& s, const Shop& sh) { return std::hypot(sh.x - s.x, sh.y - s.y); }
    template <class Choose>
    static Outcome simulate(const Case& c, const Choose& choose) {
        std::vector<int> left(c.shops.size());
        for (std::size_t k = 0; k < left.size(); ++k) left[k] = static_cast<int>(k);
        State st;
        Outcome o;
        for (;;) {
            st.remaining = static_cast<int>(left.size());
            std::vector<int> ok;  // shops you can still get into and finish before closing
            for (int k : left) {
                const Shop& sh = c.shops[static_cast<std::size_t>(k)];
                const double start = std::max(st.now + dist(st, sh) / kSpeed, sh.open);
                if (start + sh.duration <= sh.close) ok.push_back(k);
            }
            if (ok.empty()) break;
            const int k = choose(ok, st);
            const Shop& sh = c.shops[static_cast<std::size_t>(k)];
            st.now = std::max(st.now + dist(st, sh) / kSpeed, sh.open) + sh.duration;
            st.x = sh.x;
            st.y = sh.y;
            ++o.done;
            left.erase(std::find(left.begin(), left.end(), k));
        }
        o.end = st.now;
        return o;
    }
    // Errands done count most; finishing earlier breaks ties (one errand outweighs a whole day).
    static double score(const Case& c, const Outcome& o) {
        return static_cast<double>(o.done) / static_cast<double>(c.shops.size()) - 0.1 * o.end / 600.0;
    }
    const std::vector<Case>& cases(Split s) const { return s == Split::Train ? train_ : test_; }
    std::vector<Case> train_, test_;
};

ADAPTOGEN_PLUGIN(Errands)
