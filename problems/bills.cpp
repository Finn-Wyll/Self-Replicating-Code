// adaptogen-problem: bills: Manage a year of income, bills and surprise expenses: pay on time, keep a buffer, save the rest (evolves a money rule)
//
// Each month: income arrives, then the evolved program is used two ways (it is told which via
// decision.kind):
//   decision.kind = 0: it scores each bill due this month; bills are paid in that order while money
//                      lasts. Unpaid bills cost a late fee and roll over; an unpaid essential bill
//                      (rent) is a serious problem.
//   decision.kind = 1: its output (0..1) is the share of the money left over that goes to savings.
// Surprise expenses (repairs, medical) come out of cash first, then savings; anything still
// missing becomes debt at 2% a month. Score: final net worth minus fees, debt and missed rent,
// relative to a year's income.
#include "rules.hpp"

#include <algorithm>

using namespace adaptogen::sdk;

class Bills : public Problem {
public:
    Bills(const Params& p, std::uint64_t seed) {
        Rng rng(seed);
        auto make = [&](int n_cases, std::vector<Year>& out) {
            for (int c = 0; c < n_cases; ++c) {
                Year y;
                const double base = rng.uniform(1800, 3200);
                for (int m = 0; m < 12; ++m) {
                    Month mo;
                    mo.income = base * rng.uniform(0.8, 1.2);
                    mo.bills = {{base * rng.uniform(0.32, 0.4), 50, 1, true},   // rent
                                {rng.uniform(80, 220), 15, 10, false},          // utilities
                                {rng.uniform(30, 70), 10, 15, false},           // phone
                                {rng.uniform(100, 300), 35, 20, false}};        // credit card
                    if (rng.chance(0.25)) mo.surprise = rng.uniform(200, 1500);  // repair, doctor, ...
                    y.months.push_back(mo);
                }
                y.income = 0;
                for (const auto& m : y.months) y.income += m.income;
                out.push_back(std::move(y));
            }
        };
        make(p.get("train_cases", 60), train_);
        make(p.get("test_cases", 150), test_);
    }

    std::string name() const override { return "bills"; }
    std::string description() const override {
        return "Pay monthly bills on time, cover surprise expenses and save as much as possible over a year";
    }
    std::vector<Channel> channels() const override {
        return {sensor("decision.kind", 0, "0 = ranking a bill to pay, 1 = choosing the share of leftover money to save"),
                sensor("item.amount", 0, "amount of this bill", "EUR"),
                sensor("item.late_fee", 0, "fee if this bill is paid late", "EUR"),
                sensor("item.due_day", 0, "day of the month this bill is due"),
                sensor("item.essential", 0, "1 if missing this bill is serious (rent), else 0"),
                sensor("money.cash", 0, "money available right now", "EUR"),
                sensor("money.savings", 0, "money in savings", "EUR"),
                sensor("money.debt", 0, "money owed (2% interest a month)", "EUR"),
                sensor("time.month", 0, "month of the year (0..11)")};
    }
    std::size_t num_cases(Split s) const override { return years(s).size(); }

    double evaluate_case(Runner& run, Split s, std::size_t i) const override {
        const Year& y = years(s)[i];
        const Result r = simulate(y, [&](const std::vector<Bill>& due, const Wallet& w, int month) {
            // ranking: run once per due bill
            std::vector<int> order;
            std::vector<int> left(due.size());
            for (std::size_t k = 0; k < left.size(); ++k) left[k] = static_cast<int>(k);
            while (!left.empty()) {
                const int pick = rules::pick(run, static_cast<int>(left.size()), [&](int k, int ch, const double*) {
                    return sense(ch, 0, &due[static_cast<std::size_t>(left[static_cast<std::size_t>(k)])], w, month);
                });
                order.push_back(left[static_cast<std::size_t>(pick)]);
                left.erase(left.begin() + pick);
            }
            return order;
        }, [&](const Wallet& w, int month) {
            run.reset();
            auto env = make_env([&](int ch, const double*, int) { return sense(ch, 1, nullptr, w, month); });
            run(env);
            const double v = run.reg(0);
            return std::isfinite(v) ? std::clamp(v, 0.0, 1.0) : 0.0;
        });
        return r.score / y.income;
    }

    std::vector<Metric> metrics(const Evaluator& eval, Split s) const override {
        std::vector<double> mine, simple;
        for (std::size_t i = 0; i < num_cases(s); ++i) {
            const Year& y = years(s)[i];
            mine.push_back(eval.score(s, i));
            // simple rule: pay bills in due-date order, save half of what is left
            simple.push_back(simulate(y, [](const std::vector<Bill>& due, const Wallet&, int) {
                std::vector<int> o(due.size());
                for (std::size_t k = 0; k < o.size(); ++k) o[k] = static_cast<int>(k);
                std::sort(o.begin(), o.end(), [&](int a, int b) { return due[a].due_day < due[b].due_day; });
                return o;
            }, [](const Wallet&, int) { return 0.5; }).score / y.income);
        }
        return {{"net_worth_share_of_income", rules::mean(mine)}, {"simple_rule_share", rules::mean(simple)}};
    }

private:
    struct Bill {
        double amount = 0, late_fee = 0, due_day = 1;
        bool essential = false;
    };
    struct Month {
        double income = 0, surprise = 0;
        std::vector<Bill> bills;
    };
    struct Year {
        std::vector<Month> months;
        double income = 1;
    };
    struct Wallet {
        double cash = 0, savings = 0, debt = 0;
    };
    struct Result {
        double score = 0;
    };
    static double sense(int ch, int kind, const Bill* b, const Wallet& w, int month) {
        switch (ch) {
            case 0: return kind;
            case 1: return b ? b->amount : 0.0;
            case 2: return b ? b->late_fee : 0.0;
            case 3: return b ? b->due_day : 0.0;
            case 4: return b && b->essential ? 1.0 : 0.0;
            case 5: return w.cash;
            case 6: return w.savings;
            case 7: return w.debt;
            case 8: return month;
        }
        return 0.0;
    }
    template <class Order, class Save>
    static Result simulate(const Year& y, const Order& order, const Save& save) {
        Wallet w;
        double fees = 0, missed_rent = 0;
        std::vector<Bill> carried;
        for (int m = 0; m < 12; ++m) {
            const Month& mo = y.months[static_cast<std::size_t>(m)];
            w.cash += mo.income;
            w.debt *= 1.02;
            std::vector<Bill> due = carried;
            due.insert(due.end(), mo.bills.begin(), mo.bills.end());
            carried.clear();
            for (int k : order(due, w, m)) {
                Bill b = due[static_cast<std::size_t>(k)];
                if (w.cash >= b.amount) {
                    w.cash -= b.amount;
                } else {
                    fees += b.late_fee;
                    if (b.essential) missed_rent += 1;
                    b.amount += b.late_fee;
                    carried.push_back(b);
                }
            }
            if (mo.surprise > 0) {  // cash first, then savings, then debt
                double need = mo.surprise;
                const double from_cash = std::min(need, w.cash);
                w.cash -= from_cash;
                need -= from_cash;
                const double from_savings = std::min(need, w.savings);
                w.savings -= from_savings;
                need -= from_savings;
                w.debt += need;
            }
            if (w.debt > 0) {  // pay debt back from cash first
                const double pay = std::min(w.debt, w.cash);
                w.debt -= pay;
                w.cash -= pay;
            }
            const double share = save(w, m);
            w.savings += share * w.cash;
            w.cash -= share * w.cash;
        }
        double unpaid = 0;
        for (const auto& b : carried) unpaid += b.amount;
        // Savings earn a little; cash does not. Late fees and missed rent are what really hurt.
        return {w.savings * 1.02 + w.cash - w.debt - unpaid - 2 * fees - 1000 * missed_rent};
    }
    const std::vector<Year>& years(Split s) const { return s == Split::Train ? train_ : test_; }
    std::vector<Year> train_, test_;
};

ADAPTOGEN_PLUGIN(Bills)
