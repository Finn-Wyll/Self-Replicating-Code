#include "common.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace adaptogen::cli {

Options& Options::value(std::initializer_list<const char*> names, std::function<void(const std::string&)> fn) {
    options_.push_back({{names.begin(), names.end()}, true, std::move(fn)});
    return *this;
}

Options& Options::flag(std::initializer_list<const char*> names, std::function<void()> fn) {
    options_.push_back({{names.begin(), names.end()}, false, [fn = std::move(fn)](const std::string&) { fn(); }});
    return *this;
}

Options& Options::positional(std::function<void(const std::string&)> fn) {
    positional_ = std::move(fn);
    return *this;
}

void Options::parse(const Args& args) const {
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        const Option* opt = nullptr;
        for (const auto& o : options_)
            for (const auto& n : o.names)
                if (n == a) opt = &o;
        if (opt) {
            if (!opt->takes_value) opt->fn("");
            else if (i + 1 >= args.size()) throw std::runtime_error(a + " needs a value");
            else opt->fn(args[++i]);
        } else if (positional_ && !a.empty() && a[0] != '-') {
            positional_(a);
        } else {
            throw std::runtime_error("unknown option '" + a + "'");
        }
    }
}

std::string default_library() {
    const char* env = std::getenv("ADAPTOGEN_LIBRARY");
    return env && *env ? env : "strains";
}

json parse_value(const std::string& s) {
    try {
        return json::parse(s);
    } catch (...) {
        return s;
    }
}

std::pair<std::string, json> key_value(const std::string& kv) {
    const auto eq = kv.find('=');
    if (eq == std::string::npos) throw std::runtime_error("expected KEY=VALUE, got '" + kv + "'");
    return {kv.substr(0, eq), parse_value(kv.substr(eq + 1))};
}

std::pair<std::string, std::string> mapping(const std::string& kv, const char* what) {
    const auto eq = kv.find('=');
    if (eq == std::string::npos) throw std::runtime_error(std::string(what) + " expects OLD=NEW");
    return {kv.substr(0, eq), kv.substr(eq + 1)};
}

json read_json(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot open '" + path + "'");
    return json::parse(in);
}

json json_arg(const std::string& v) {
    if (!v.empty() && v[0] == '@') return read_json(v.substr(1));
    return json::parse(v);
}

std::string metrics_line(const std::vector<Metric>& ms) {
    std::string s;
    char buf[64];
    for (const auto& m : ms) {
        std::snprintf(buf, sizeof buf, "%s%s=%.6g", s.empty() ? "" : " ", m.name.c_str(), m.value);
        s += buf;
    }
    return s;
}

void write_text(const std::string& path, const std::string& text) {
    if (path.empty()) {
        std::cout << text;
        return;
    }
    std::ofstream out(path);
    if (!out) throw std::runtime_error("cannot write '" + path + "'");
    out << text;
    std::printf("wrote %s\n", path.c_str());
}

void print_compatibility(const std::string& id, const Compatibility& c) {
    auto join = [](const std::vector<std::string>& v) {
        std::string s;
        for (const auto& x : v) s += (s.empty() ? "" : ", ") + x;
        return s.empty() ? std::string("-") : s;
    };
    std::printf("strain %s: %.0f%% of the channels its code uses are available\n", id.c_str(), 100 * c.coverage());
    std::printf("  matched: %s\n  missing (code becomes pseudogenes): %s\n  new (available to evolution): %s\n",
                join(c.matched).c_str(), join(c.missing).c_str(), join(c.added).c_str());
    for (const auto& w : c.warnings) std::printf("  warning: %s\n", w.c_str());
    std::printf("  (* = used by the strain's effective code)\n");
}

json compatibility_json(const std::string& id, const Compatibility& c) {
    return json{{"strain", id},         {"coverage", c.coverage()}, {"matched", c.matched},
                {"missing", c.missing}, {"added", c.added},         {"warnings", c.warnings}};
}

json diagnostics_json(const BuildResult& b) {
    json d = json::array();
    for (const auto& x : b.diagnostics)
        d.push_back({{"file", x.file}, {"line", x.line}, {"column", x.column}, {"severity", x.severity}, {"message", x.message}});
    return d;
}

}  // namespace adaptogen::cli
