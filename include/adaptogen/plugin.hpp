#pragma once
// Plug-in problems: build a .cpp into a shared library, run it in isolated worker processes,
// validate it, and generate C++ base classes from strains.
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "adaptogen/problem.hpp"
#include "adaptogen/strain.hpp"

namespace adaptogen {

// ------------------------------------------------------------------ building

struct Diagnostic {
    std::string file, severity, message;
    int line = 0, column = 0;
};

struct BuildResult {
    bool ok = false;
    bool cached = false;
    std::filesystem::path library;  // the built shared library
    std::string command, output;
    std::vector<Diagnostic> diagnostics;
};

/// Compile a plug-in source file into a shared library (cached by content, compiler and SDK).
/// Compiler: $ADAPTOGEN_CXX, $CXX, or the one adaptogen was built with.
BuildResult build_plugin(const std::filesystem::path& source);

bool is_plugin_source(const std::filesystem::path& p);

// ------------------------------------------------------------------ running

/// A problem implemented by a plug-in, running in separate worker processes. A crash or hang
/// (timeout) of the plug-in kills only a worker: it is restarted and the program that caused
/// it scores as badly as possible.
class PluginProblem : public Problem {
public:
    /// library: a built plug-in (.so/.dylib). Use make_plugin_problem() for sources.
    PluginProblem(std::filesystem::path library, nlohmann::json params, std::uint64_t seed);
    ~PluginProblem() override;

    std::string name() const override { return info_.value("name", "plugin"); }
    std::string description() const override { return info_.value("description", ""); }
    std::vector<Channel> channels() const override { return channels_; }
    int num_outputs() const override { return info_.value("num_outputs", 1); }
    std::vector<Op> default_ops() const override;
    std::size_t num_cases(Split s) const override;
    double evaluate_case(Runner& runner, const Program& program, Split split, std::size_t index) const override;
    void evaluate_batch(const MachineSpec& spec, const std::vector<const Program*>& programs, Split split,
                        const std::vector<std::size_t>& cases, int threads, std::vector<double>& scores,
                        std::vector<long>& steps) const override;
    std::vector<Metric> metrics(const MachineSpec& spec, const Program& program, Split split) const override;
    bool solved(const MachineSpec& spec, const Program& program, Split split) const override;

    const nlohmann::json& info() const { return info_; }
    std::vector<std::string> parents() const;
    /// Channel maps the plug-in declares for its parents ("strain:old" -> "new").
    std::vector<std::pair<std::string, std::string>> channel_maps() const;
    /// Programs that crashed or hung a worker so far.
    long failures() const { return failures_; }

    static constexpr double kFailureScore = -1e12;
    double timeout_seconds = 60.0;  // per request

private:
    struct Worker;
    std::shared_ptr<Worker> acquire(std::size_t slot) const;
    nlohmann::json request(Worker& w, const nlohmann::json& req, const MachineSpec* spec) const;
    void score_chunk(std::size_t slot, const MachineSpec& spec, const std::vector<const Program*>& programs,
                     std::size_t begin, std::size_t end, Split split, const std::vector<std::size_t>& cases,
                     std::vector<double>& scores, std::vector<long>& steps) const;
    std::pair<std::vector<Metric>, bool> metrics_and_solved(const MachineSpec& spec, const Program& p, Split s) const;

    std::filesystem::path library_;
    nlohmann::json params_;
    std::uint64_t seed_;
    nlohmann::json info_;
    std::vector<Channel> channels_;
    mutable std::mutex mu_;
    mutable std::vector<std::shared_ptr<Worker>> workers_;
    mutable long failures_ = 0;
};

/// Start evolution from a plug-in's parent strains, with the channel maps it declares. Throws if
/// the problem is not a plug-in derived from strains.
void seed_from_parents(const Problem& problem, EngineConfig& cfg);

/// "plugin:<path>" problems: builds sources first (throws with compiler diagnostics on failure).
std::unique_ptr<PluginProblem> make_plugin_problem(const std::string& path, const nlohmann::json& params,
                                                   std::uint64_t seed);

// ------------------------------------------------------------------ validation

struct ValidationReport {
    bool ok = false;
    std::vector<std::string> errors, warnings, notes;
    nlohmann::json info;
    BuildResult build;
};

/// Build (if a source), load in a worker and check: interface sanity, inheritance rules against
/// the parent strains (if any, looked up in `library`), determinism, finite scores, metrics.
ValidationReport validate_plugin(const std::filesystem::path& path, const nlohmann::json& params, std::uint64_t seed,
                                 const std::filesystem::path& library);

// ------------------------------------------------------------------ strains as base classes

/// Generate a C++ base class (for the SDK) from one or more strains. The class fixes the
/// inherited channels (identical names, kinds, arities, descriptions, units) and records the
/// parents. Channels with the same name but different definitions in different parents are a
/// conflict: resolve them with renames {"strain-id:channel", "new.name"}; otherwise this throws
/// and lists the conflicts.
std::string generate_base_header(const std::vector<Strain>& parents, const std::string& class_name,
                                 const std::vector<std::pair<std::string, std::string>>& renames = {});

/// A ready-to-fill plug-in source file, optionally deriving from a generated base header.
std::string plugin_template(const std::string& problem_name, const std::string& class_name,
                            const std::string& base_header = "", const std::string& base_class = "");

}  // namespace adaptogen
