#pragma once
// Verified program simplification: remove code without changing what the program does on the
// cases it is checked against.
//
// The program is parsed into its statement structure (if-blocks and loop bodies), then the
// simplifier repeatedly tries to delete statements and to unwrap ifs/loops. A change is kept only
// if no case scores worse than before (per-case, within a tolerance). Static intron analysis is
// conservative around loops, so checking behaviour directly removes far more dead code safely.
#include "adaptogen/problem.hpp"

namespace adaptogen {

struct SimplifyOptions {
    Split split = Split::Train;  // cases the behaviour is checked on (test data is never used)
    double tolerance = 1e-9;     // a case may not score lower than before by more than this
    int max_passes = 20;
};

struct SimplifyResult {
    Program program;
    std::size_t length_before = 0, length_after = 0;
    int accepted_changes = 0;
    long program_evaluations = 0;  // verification cost
};

SimplifyResult simplify(const Problem& problem, const MachineSpec& spec, const Program& program,
                        const SimplifyOptions& options = {});

}  // namespace adaptogen
