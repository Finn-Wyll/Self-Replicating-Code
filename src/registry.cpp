#include "adaptogen/registry.hpp"

#include <filesystem>
#include <fstream>
#include <mutex>
#include <algorithm>
#include <cstdlib>
#include <stdexcept>

#include "adaptogen/problems/formula.hpp"
#include "adaptogen/problems/lists.hpp"
#include "adaptogen/build_config.hpp"
#include "adaptogen/plugin.hpp"

namespace adaptogen {

namespace {
std::vector<ProblemEntry>& entries() {
    static std::vector<ProblemEntry> e;
    return e;
}
}  // namespace

namespace {
void register_plugin_directory(const std::filesystem::path& dir);
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
        const char* dir = std::getenv("ADAPTOGEN_PROBLEMS");
        register_plugin_directory(dir && *dir ? dir : ADAPTOGEN_PROBLEMS_DIR);
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
                          "{\"task\": \"sum\"|\"max\"|\"count_positive\", \"train_cases\": 100, \"train_max_length\": 20, "
                          "\"test_cases\": 200, \"test_max_length\": 50, \"value_range\": 9}",
                          [](const nlohmann::json& p, std::uint64_t seed) -> std::unique_ptr<Problem> {
                              return ListProblem::from_params(p, seed);
                          }});
    });
}

namespace {
// Built-in plug-in problems: every problems/*.cpp whose first line reads
//   // adaptogen-problem: <name>: <description>
// is registered under <name> (built on first use, cached).
void register_plugin_directory(const std::filesystem::path& dir) {
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) return;
    std::vector<std::filesystem::path> files;
    for (const auto& f : std::filesystem::directory_iterator(dir, ec))
        if (f.path().extension() == ".cpp") files.push_back(f.path());
    std::sort(files.begin(), files.end());
    for (const auto& f : files) {
        std::ifstream in(f);
        std::string line;
        std::getline(in, line);
        const std::string tag = "// adaptogen-problem: ";
        if (line.rfind(tag, 0) != 0) continue;
        const std::string rest = line.substr(tag.size());
        const auto colon = rest.find(':');
        if (colon == std::string::npos) continue;
        const std::string name = rest.substr(0, colon);
        std::string desc = rest.substr(colon + 1);
        desc.erase(0, desc.find_first_not_of(' '));
        const std::string path = f.string();
        register_problem({name, desc + " [plug-in: " + f.filename().string() + "]",
                          "see the parameters read in " + path,
                          [path](const nlohmann::json& p, std::uint64_t seed) -> std::unique_ptr<Problem> {
                              return make_plugin_problem(path, p, seed);
                          }});
    }
}
}  // namespace

const std::vector<ProblemEntry>& registered_problems() { return entries(); }

std::unique_ptr<Problem> make_problem(const std::string& name, const nlohmann::json& params, std::uint64_t seed) {
    if (name.rfind("plugin:", 0) == 0) return make_plugin_problem(name.substr(7), params, seed);
    for (const auto& e : entries())
        if (e.name == name) return e.factory(params, seed);
    throw std::runtime_error("unknown problem '" + name + "' (try: adaptogen list, or plugin:FILE.cpp)");
}

}  // namespace adaptogen
