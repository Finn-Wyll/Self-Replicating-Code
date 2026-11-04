#pragma once
// The strain bank: saved evolved programs that can be revived on new problems.
//
// A strain bundles
//   - its interface: the channels it evolved with (name, kind, arity, description, units) and
//     which of them its code actually uses,
//   - its behaviour: the evolved program (channels referenced by name),
//   - its lineage: parent strains, the problem it evolved on, the settings and its scores.
// Saved strains are frozen: saving a name again creates a new version (name-v2, name-v3, ...).
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "adaptogen/engine.hpp"

namespace adaptogen {

struct Strain {
    std::string id;  // "<name>-v<version>"
    std::string name;
    int version = 0;
    std::string description;
    std::string created;  // ISO 8601 UTC

    // interface
    std::vector<Channel> channels;
    std::vector<bool> channel_used;
    int num_outputs = 1;
    int num_registers = 8;
    std::vector<Op> ops;

    // behaviour
    nlohmann::json program;  // program JSON (see serialize.hpp)

    // lineage
    std::vector<std::string> parents;  // ids of the strains this one was seeded from
    nlohmann::json problem;            // {"name", "params", "seed"}: how to rebuild its problem
    nlohmann::json config;             // engine config of the run
    int generations = 0;
    long program_evaluations = 0;
    nlohmann::json train_metrics, test_metrics;
    bool solved_test = false;
    nlohmann::json events = nlohmann::json::array();  // improvements during its run: generation, fitness, origin
    nlohmann::json extras = nlohmann::json::object();  // problem-specific metadata (e.g. decision calibration)
};

nlohmann::json strain_to_json(const Strain& s);
Strain strain_from_json(const nlohmann::json& j);

/// Build a strain from a finished run. The id/version are assigned when it is saved.
Strain make_strain(const std::string& name, const std::string& description, const Problem& problem,
                   const MachineSpec& spec, const EvolutionResult& result, const nlohmann::json& problem_spec,
                   const EngineConfig& config, std::vector<std::string> parents);

struct StrainIndexEntry {
    std::string id, name, description, problem, created;
    int version = 0;
    std::vector<std::string> channels, parents;
    nlohmann::json test_metrics;
    bool solved_test = false;
};

class StrainLibrary {
public:
    explicit StrainLibrary(std::filesystem::path dir);
    const std::filesystem::path& dir() const { return dir_; }

    /// Save as the next version of s.name; returns the new id. Existing files are never overwritten.
    std::string save(Strain s);
    /// Load by id ("sum-v2") or by name (latest version).
    Strain load(const std::string& id_or_name) const;
    std::vector<StrainIndexEntry> list() const;
    /// Rebuild index.json from the strain files.
    void reindex() const;
    /// The strain and all its ancestors (breadth-first, each once). Missing parents are skipped.
    std::vector<Strain> ancestry(const std::string& id_or_name) const;

    static bool valid_name(const std::string& name);

private:
    std::filesystem::path dir_;
    std::vector<StrainIndexEntry> read_index() const;
    void write_index(const std::vector<StrainIndexEntry>& entries) const;
};

/// How a strain's interface lines up with a problem's channels.
struct Compatibility {
    std::vector<std::string> matched;   // strain channel -> problem channel (same kind and arity)
    std::vector<std::string> missing;   // strain channels with no counterpart: their code becomes pseudogenes
    std::vector<std::string> added;     // problem channels the strain never had: new to explore
    std::vector<std::string> warnings;  // e.g. same name but different units: meaning may differ
    int used_missing = 0;               // missing channels the strain's code actually used
    double coverage() const;            // fraction of the strain's used channels that matched
};

using ChannelMap = std::vector<std::pair<std::string, std::string>>;

Compatibility check_compatibility(const Strain& strain, const std::vector<Channel>& problem_channels,
                                  const ChannelMap& map = {});

/// The strain's program translated to a target machine (channels matched by name/kind/arity
/// after applying the map; unmatched references become Nop pseudogenes).
Program revive(const Strain& strain, const MachineSpec& target, const ChannelMap& map = {});

/// Everything a seeded/retained run needs, prepared from config.seeding.
struct PreparedRun {
    std::unique_ptr<Problem> retention;  // composite problem when retain_weight > 0
    std::vector<std::unique_ptr<Problem>> parent_problems;
    std::vector<Strain> strains;
    std::vector<Compatibility> compatibility;
    std::vector<Program> seeds;  // revived programs (on the target machine)
    const Problem& problem(const Problem& primary) const { return retention ? *retention : primary; }
};

/// Load the strains named in cfg.seeding, check compatibility, revive their programs and, if
/// retain_weight > 0, rebuild the parents' problems (from their lineage) for forgetting control.
PreparedRun prepare_seeding(const Problem& primary, const EngineConfig& cfg);

/// Run evolution honouring cfg.seeding (seeding and retention). Without seeding this is
/// exactly Engine(problem, cfg).run().
EvolutionResult run_evolution(const Problem& problem, const EngineConfig& cfg,
                              const std::function<void(const GenerationReport&)>& on_generation = {},
                              PreparedRun* prepared_out = nullptr, const std::atomic<bool>* stop = nullptr);

/// Wraps a problem and keeps scoring programs on parent problems too ("forgetting control").
/// Training cases = primary cases followed by every parent's cases, rescaled so the mean case
/// score equals (1 - w) * primary mean + w * parents' mean. Test split, metrics and `solved`
/// are the primary problem's, plus a retained score per parent.
class RetentionProblem : public Problem {
public:
    RetentionProblem(const Problem& primary, std::vector<const Problem*> parents, double weight,
                     const EngineConfig& cfg);
    std::string name() const override { return primary_.name(); }
    std::string description() const override;
    std::vector<Channel> channels() const override { return primary_.channels(); }
    int num_outputs() const override { return primary_.num_outputs(); }
    std::vector<Op> default_ops() const override { return primary_.default_ops(); }
    std::size_t num_cases(Split s) const override;
    double evaluate_case(Runner& runner, const Program& program, Split split, std::size_t index) const override;
    std::vector<Metric> metrics(const MachineSpec& spec, const Program& program, Split split) const override;
    bool solved(const MachineSpec& spec, const Program& program, Split split) const override {
        return primary_.solved(spec, program, split);
    }
    nlohmann::json strain_extras(const MachineSpec& spec, const Program& program) const override {
        return primary_.strain_extras(spec, program);
    }

private:
    struct Parent {
        const Problem* problem;
        MachineSpec spec;
        std::vector<int> channel_to_parent;  // primary channel index -> parent channel index (-1 = none)
    };
    Program translate(const Program& p, const Parent& parent) const;
    const Problem& primary_;
    std::vector<Parent> parents_;
    double weight_;
};

}  // namespace adaptogen
