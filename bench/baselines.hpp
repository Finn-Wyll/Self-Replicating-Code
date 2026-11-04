#pragma once
// Reference algorithms used to judge adaptogen. All share the same evaluation budget
// (programs evaluated on the full training set) and the same success criterion.
#include <string>

#include "adaptogen/engine.hpp"
#include "adaptogen/problems/formula.hpp"

namespace adaptogen::bench {

struct AlgoResult {
    double test_primary = 0;  // first test metric of the problem (R^2 for formulas, accuracy for lists)
    bool solved_test = false;
    long program_evaluations = 0;
    double seconds = 0;
    double size = 0;  // program length / tree nodes / number of coefficients
};

/// adaptogen's engine (linear programs on the VM).
AlgoResult run_adaptogen(const Problem& problem, const EngineConfig& cfg);
/// Random programs, keep the best. Same budget as population * generations.
AlgoResult run_random_search(const Problem& problem, const EngineConfig& cfg);
/// (1+1) hill climbing on linear programs with restarts.
AlgoResult run_hill_climber(const Problem& problem, const EngineConfig& cfg);
/// Classic Koza-style tree GP: ramped half-and-half, subtree crossover/mutation, tournament 7.
/// Formula problems only.
AlgoResult run_tree_gp(const FormulaProblem& problem, const EngineConfig& cfg);
/// Polynomial least squares; degree chosen on a hold-out split (not evolutionary). Formula problems only.
AlgoResult run_polyfit(const FormulaProblem& problem, const EngineConfig& cfg);

}  // namespace adaptogen::bench
