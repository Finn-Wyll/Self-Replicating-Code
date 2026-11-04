#include "adaptogen/registry.hpp"

#include <mutex>
#include <stdexcept>

#include "adaptogen/problems/formula.hpp"
#include "adaptogen/problems/lists.hpp"

namespace adaptogen {

namespace {
std::vector<ProblemEntry>& entries() {
    static std::vector<ProblemEntry> e;
    return e;
}
}  // namespace

void register_problem(ProblemEntry entry) {
    for (auto& e : entries())
        if (e.name == entry.name) {
            e = std::move(entry);
            return;
        }
    entries().push_back(std::move(entry));
}

void register_builtin_problems() {
    static std::once_flag once;
    std::call_once(once, [] {
        std::string targets;
        for (const auto& t : formula_targets()) targets += "\n      " + t.name + ": " + t.formula;
        register_problem({"formula",
                          "Formula discovery: find a program reproducing a hidden function from examples.",
                          "{\"target\": <benchmark>} or {\"data\": \"file.csv\", \"target_column\": \"y\", "
                          "\"test_fraction\": 0.25, \"noise\": 0.0}\n    benchmarks:" + targets,
                          [](const nlohmann::json& p, std::uint64_t seed) -> std::unique_ptr<Problem> {
                              return FormulaProblem::from_params(p, seed);
                          }});
        register_problem({"list",
                          "List algorithms: compute a property of a list (programs must discover iteration).",
                          "{\"task\": \"sum\"|\"max\"|\"count_positive\", \"train_cases\": 60, \"train_max_length\": 10, "
                          "\"test_cases\": 200, \"test_max_length\": 40, \"value_range\": 9}",
                          [](const nlohmann::json& p, std::uint64_t seed) -> std::unique_ptr<Problem> {
                              return ListProblem::from_params(p, seed);
                          }});
    });
}

const std::vector<ProblemEntry>& registered_problems() { return entries(); }

std::unique_ptr<Problem> make_problem(const std::string& name, const nlohmann::json& params, std::uint64_t seed) {
    for (const auto& e : entries())
        if (e.name == name) return e.factory(params, seed);
    throw std::runtime_error("unknown problem '" + name + "' (try: adaptogen list)");
}

}  // namespace adaptogen
