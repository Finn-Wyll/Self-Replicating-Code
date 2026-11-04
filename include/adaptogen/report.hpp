#pragma once
// Output tools: what changed between programs, how a solution evolved, and exporting an evolved
// program as standalone C++ code.
#include <string>

#include "adaptogen/engine.hpp"
#include "adaptogen/strain.hpp"

namespace adaptogen {

/// Line diff of the effective (readable) code of two programs: "+ " added, "- " removed,
/// "  " unchanged. Both programs may live on different machines (e.g. parent and derived strain).
std::string program_diff(const MachineSpec& before_spec, const Program& before, const MachineSpec& after_spec,
                         const Program& after, int num_outputs);

/// Markdown report of a run: every improvement of the champion, what produced it and the code change.
std::string lineage_report(const MachineSpec& spec, int num_outputs, const EvolutionResult& result,
                           const std::string& title);

/// The machine a saved strain's program runs on (its own interface and instruction set).
MachineSpec strain_machine(const Strain& s);

/// Markdown report of a strain's ancestry (oldest first): problems, scores and how the behaviour
/// changed from each parent to its child.
std::string strain_lineage_report(const StrainLibrary& library, const std::string& id_or_name);

/// Export a program as a self-contained C++ header that computes exactly what the VM computes
/// (same protected operators, loop caps and step limit). `name` becomes the namespace.
std::string export_cpp(const MachineSpec& spec, const Program& program, int num_outputs, const std::string& name,
                       const std::string& comment = "");

enum class ExportLanguage { Cpp, Python, JavaScript };
/// "cpp" | "c++" | "python" | "py" | "javascript" | "js" | "typescript" | "ts"
ExportLanguage export_language(const std::string& name);
const char* file_extension(ExportLanguage lang);

/// Export a program as plug-in code for an app. Every language gets an exact run() and, when the
/// program only reads plain numbers (sensors without arguments, no actions), a choose(options,
/// context) that scores each option and returns the best (with calibrated confidence for
/// decision strains), and score(values) for single-answer programs. `header` lines become the
/// file's leading comment (e.g. the report card).
std::string export_code(ExportLanguage lang, const MachineSpec& spec, const Program& program, int num_outputs,
                        const std::string& name, const std::vector<std::string>& header = {},
                        const nlohmann::json& extras = nlohmann::json::object());

/// Held-out performance of a strain compared with the simple solutions, as plain text lines.
std::vector<std::string> report_card(const Strain& s);

}  // namespace adaptogen
