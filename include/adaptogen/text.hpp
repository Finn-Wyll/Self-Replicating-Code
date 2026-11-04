#pragma once
// Small text helpers shared by the code generators and the command-line tools.
#include <cctype>
#include <string>

namespace adaptogen {

/// A valid C/C++/JS identifier from any name ("list.item" -> "list_item", "3d" -> "_3d").
inline std::string cpp_identifier(const std::string& s) {
    std::string out;
    for (char c : s) out += std::isalnum(static_cast<unsigned char>(c)) ? c : '_';
    if (out.empty() || std::isdigit(static_cast<unsigned char>(out[0]))) out = "_" + out;
    return out;
}

/// A class name from a problem name ("weighted_sum" -> "WeightedSum", "3d" -> "P3d").
inline std::string class_name_for(const std::string& s) {
    std::string out;
    bool up = true;
    for (char c : s) {
        if (!std::isalnum(static_cast<unsigned char>(c))) {
            up = true;
            continue;
        }
        out += up ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : c;
        up = false;
    }
    return out.empty() || std::isdigit(static_cast<unsigned char>(out[0])) ? "P" + out : out;
}

}  // namespace adaptogen
