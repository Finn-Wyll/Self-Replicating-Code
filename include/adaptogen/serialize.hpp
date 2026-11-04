#pragma once
// JSON for programs and configs. Channels are stored by name so programs can later be
// revived on other problems that share (or inherit) the same channels.
#include <nlohmann/json.hpp>

#include "adaptogen/engine.hpp"

namespace adaptogen {

nlohmann::json program_to_json(const MachineSpec& spec, const Program& p);
/// Unknown channel names become Nop (pseudogenes); register indices wrap to the machine.
Program program_from_json(const MachineSpec& spec, const nlohmann::json& j);

nlohmann::json config_to_json(const EngineConfig& c);
/// Overlay the keys present in j onto c (unknown keys raise an error).
void config_from_json(EngineConfig& c, const nlohmann::json& j);

/// Metrics as a JSON object {name: value}.
nlohmann::json metrics_to_json(const std::vector<Metric>& metrics);

nlohmann::json report_to_json(const GenerationReport& r);
nlohmann::json result_to_json(const MachineSpec& spec, const EvolutionResult& r);

}  // namespace adaptogen
