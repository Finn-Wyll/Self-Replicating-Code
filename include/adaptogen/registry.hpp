#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "adaptogen/problem.hpp"

namespace adaptogen {

using ProblemFactory =
    std::function<std::unique_ptr<Problem>(const nlohmann::json& params, std::uint64_t seed)>;

struct ProblemEntry {
    std::string name;
    std::string description;
    std::string params_help;
    ProblemFactory factory;
};

void register_problem(ProblemEntry entry);
/// Registers the problems that ship with adaptogen (idempotent).
void register_builtin_problems();
const std::vector<ProblemEntry>& registered_problems();
std::unique_ptr<Problem> make_problem(const std::string& name, const nlohmann::json& params,
                                      std::uint64_t seed);

}  // namespace adaptogen
