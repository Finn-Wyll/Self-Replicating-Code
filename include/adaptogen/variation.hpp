#pragma once
// Creating and changing programs: the mutation and recombination operators.
#include "adaptogen/random.hpp"
#include "adaptogen/vm.hpp"

namespace adaptogen {

struct VariationConfig {
    int init_min_length = 4;
    int init_max_length = 20;
    int max_length = 128;
    double sense_weight = 3.0;  // how much likelier Sense is than any one other op
    double act_weight = 3.0;

    double p_crossover = 0.5;   // two-point linear crossover with a second parent
    double p_mutation = 1.0;    // chance an offspring receives a mutation event
    // relative weights of mutation kinds
    double w_insert = 1.0;      // insertion (indel)
    double w_delete = 1.0;      // deletion (indel)
    double w_point = 1.0;       // point mutation (copy error)
    double w_constant = 0.5;    // small change of a constant
    double constant_sigma = 0.5;
    /// Aim point/constant mutations at effective code and make inserted instructions
    /// effective (Brameier & Banzhaf's "effective mutation" for linear GP).
    bool effective_mutation = true;
};

Instr random_instruction(const MachineSpec& spec, const VariationConfig& cfg, Rng& rng);
Program random_program(const MachineSpec& spec, const VariationConfig& cfg, Rng& rng);
/// num_outputs: how many output registers the problem reads (used by effective mutation).
void mutate(Program& p, const MachineSpec& spec, const VariationConfig& cfg, Rng& rng, int num_outputs = 1);
Program crossover(const Program& a, const Program& b, const VariationConfig& cfg, Rng& rng);

/// Gene duplication: copy a segment of 1..max_segment instructions and insert the copy right
/// after the original (tandem duplication). The copies are then free to diverge.
bool duplicate_segment(Program& p, const VariationConfig& cfg, int max_segment, Rng& rng);

/// Horizontal gene transfer: insert a copy of a 1..max_segment instruction segment of the
/// donor at a random position of the recipient.
bool transfer_segment(Program& recipient, const Program& donor, const VariationConfig& cfg, int max_segment, Rng& rng);

}  // namespace adaptogen
