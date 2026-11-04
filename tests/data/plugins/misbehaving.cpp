// Test fixture: a plug-in that misbehaves on purpose, depending on the program's answer.
//   r0 > 1000   -> crashes (segmentation fault)
//   r0 < -1000  -> hangs (infinite loop)
//   r0 == 777   -> returns NaN
//   otherwise   -> score = -|r0 - 2x|
#include <adaptogen/sdk.hpp>

#include <csignal>

using namespace adaptogen::sdk;

class Misbehaving : public Problem {
public:
    Misbehaving(const Params&, std::uint64_t) {}
    std::string name() const override { return "misbehaving"; }
    std::string description() const override { return "crashes, hangs or returns NaN on purpose"; }
    std::vector<Channel> channels() const override { return {sensor("x", 0, "input")}; }
    std::size_t num_cases(Split) const override { return 10; }
    double evaluate_case(Runner& run, Split, std::size_t i) const override {
        auto env = make_env([&](int, const double*, int) { return static_cast<double>(i); });
        run(env);
        const double r = run.reg(0);
        if (r > 1000) std::raise(SIGSEGV);
        if (r < -1000)
            for (volatile long k = 0;; k = k + 1) {}
        if (r == 777) return std::nan("");
        return -std::fabs(r - 2.0 * static_cast<double>(i));
    }
};

ADAPTOGEN_PLUGIN(Misbehaving)
