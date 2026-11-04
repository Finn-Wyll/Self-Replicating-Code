// Test fixture: does not compile (undeclared identifier on line 9).
#include <adaptogen/sdk.hpp>
using namespace adaptogen::sdk;
class Broken : public Problem {
public:
    Broken(const Params&, std::uint64_t) {}
    std::string name() const override { return "broken"; }
    std::string description() const override { return ""; }
    std::vector<Channel> channels() const override { return {sensor("x", 0, undeclared_thing)}; }
    std::size_t num_cases(Split) const override { return 1; }
    double evaluate_case(Runner&, Split, std::size_t) const override { return 0; }
};
ADAPTOGEN_PLUGIN(Broken)
