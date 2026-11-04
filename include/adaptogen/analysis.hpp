#pragma once
#include <string>
#include <vector>

#include "adaptogen/vm.hpp"

namespace adaptogen {

/// Which instructions can influence the outputs or perform actions ("exons").
/// The rest are introns: neutral code that drift can later turn into something new.
std::vector<bool> effective_mask(const MachineSpec& spec, const Program& program, int num_outputs);
std::size_t effective_length(const MachineSpec& spec, const Program& program, int num_outputs);

/// One instruction as a readable statement, e.g. "r1 = r0 * x".
std::string format_instruction(const MachineSpec& spec, const Instr& in);

/// Indented, readable listing of a program. If effective_only, introns are hidden.
std::string to_pseudocode(const MachineSpec& spec, const Program& program, int num_outputs,
                          bool effective_only);

}  // namespace adaptogen
