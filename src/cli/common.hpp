#pragma once
// Shared pieces of the adaptogen command-line tool.
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "adaptogen/plugin.hpp"
#include "adaptogen/problem.hpp"
#include "adaptogen/strain.hpp"

namespace adaptogen::cli {

using nlohmann::json;
using Args = std::vector<std::string>;

/// A table of command-line options: each option name maps to what it does.
class Options {
public:
    Options& value(std::initializer_list<const char*> names, std::function<void(const std::string&)> fn);
    Options& flag(std::initializer_list<const char*> names, std::function<void()> fn);
    /// Arguments that do not start with '-' (default: an error).
    Options& positional(std::function<void(const std::string&)> fn);
    void parse(const Args& args) const;

private:
    struct Option {
        std::vector<std::string> names;
        bool takes_value;
        std::function<void(const std::string&)> fn;
    };
    std::vector<Option> options_;
    std::function<void(const std::string&)> positional_;
};

std::string default_library();
json parse_value(const std::string& s);                          // JSON if it parses, else the text
std::pair<std::string, json> key_value(const std::string& kv);   // KEY=VALUE
std::pair<std::string, std::string> mapping(const std::string& kv, const char* what);  // OLD=NEW
json read_json(const std::string& path);
json json_arg(const std::string& v);  // JSON text, or @file
std::string metrics_line(const std::vector<Metric>& ms);
void write_text(const std::string& path, const std::string& text);  // "" = stdout
void print_compatibility(const std::string& id, const Compatibility& c);
json compatibility_json(const std::string& id, const Compatibility& c);
json diagnostics_json(const BuildResult& b);

int cmd_list(const Args&);
int cmd_config(const Args&);
int cmd_evolve(const Args&);
int cmd_show(const Args&);
int cmd_strains(const Args&);
int cmd_decide(const Args&);
int cmd_adapt(const Args&);
int cmd_export(const Args&);
int cmd_plugin(const Args&);

}  // namespace adaptogen::cli
