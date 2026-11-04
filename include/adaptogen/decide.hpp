#pragma once
// Using a frozen strain as a fast, local decision function ("System One"): give it the options,
// get back the choice, every option's score and how confident it is.
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "adaptogen/strain.hpp"

namespace adaptogen {

struct Decision {
    int choice = -1;                       // index of the chosen option
    std::vector<double> scores;            // the strain's score for every option (higher = preferred)
    std::vector<int> ranking;              // options from most to least preferred
    double margin = 0;                     // how clearly the best beats the runner-up (0..1)
    double confidence = -1;                // calibrated chance the choice is a best option; -1 = no calibration
    std::vector<std::string> unknown_keys; // option/context keys the strain has no channel for (ignored)
    std::vector<std::string> missing;      // channels the strain uses that were not given (read as 0)
};

/// options: array of objects (feature name -> number); context: object (name -> number).
/// Keys may be given with or without their channel prefix ("price" or "option.price").
/// Works for any strain whose program scores one candidate at a time (decision and priority-rule
/// strains); for decision strains it also fills in the per-decision min/max channels and the
/// calibrated confidence.
Decision decide(const Strain& strain, const nlohmann::json& options, const nlohmann::json& context = nlohmann::json::object());

nlohmann::json to_json(const Decision& d);

}  // namespace adaptogen
