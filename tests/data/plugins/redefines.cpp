// Test fixture: claims a parent strain but changes the meaning (units) of an inherited channel.
#include <adaptogen/sdk.hpp>
using namespace adaptogen::sdk;
class Redefines : public Problem {
public:
    Redefines(const Params&, std::uint64_t) {}
    std::string name() const override { return "redefines"; }
    std::string description() const override { return "violates inheritance"; }
    std::vector<Channel> channels() const override {
        return {sensor("list.length", 0, "number of items in the list", "metres"),  // parent says "items"
                sensor("list.item", 1, "item at index i (0-based); 0 outside the list")};
    }
    std::vector<std::string> parents() const override { return {"list-sum-v6"}; }
    std::size_t num_cases(Split) const override { return 10; }
    double evaluate_case(Runner& run, Split, std::size_t) const override {
        auto env = make_env([](int, const double*, int) { return 1.0; });
        run(env);
        return -std::fabs(run.reg(0));
    }
};
ADAPTOGEN_PLUGIN(Redefines)
