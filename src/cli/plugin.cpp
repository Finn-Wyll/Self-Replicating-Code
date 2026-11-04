// adaptogen plugin new | header | build | validate | info
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>

#include "adaptogen/plugin.hpp"
#include "adaptogen/text.hpp"
#include "common.hpp"

namespace adaptogen::cli {

namespace {

void write_new_file(const std::filesystem::path& p, const std::string& text) {
    if (std::filesystem::exists(p)) throw std::runtime_error("'" + p.string() + "' already exists (not overwritten)");
    std::ofstream out(p);
    if (!out) throw std::runtime_error("cannot write '" + p.string() + "'");
    out << text;
}

}  // namespace

int cmd_plugin(const Args& all) {
    if (all.empty()) throw std::runtime_error("usage: adaptogen plugin new|header|build|validate|info ...");
    const std::string sub = all[0];
    const Args args(all.begin() + 1, all.end());
    std::string library = default_library(), class_name, out_file, dir = ".";
    std::vector<std::string> positional, from;
    std::vector<std::pair<std::string, std::string>> renames;
    json params = json::object();
    bool json_out = false;
    Options()
        .value({"--library"}, [&](const std::string& v) { library = v; })
        .value({"--class"}, [&](const std::string& v) { class_name = v; })
        .value({"-o", "--out"}, [&](const std::string& v) { out_file = v; })
        .value({"--dir"}, [&](const std::string& v) { dir = v; })
        .value({"--from"}, [&](const std::string& v) { from.push_back(v); })
        .value({"--params"}, [&](const std::string& v) { params.update(json::parse(v)); })
        .value({"--param"}, [&](const std::string& v) { auto [k, x] = key_value(v); params[k] = x; })
        .value({"--rename"},
               [&](const std::string& v) {
                   const auto eq = v.find('=');
                   if (eq == std::string::npos || v.find(':') > eq) throw std::runtime_error("--rename expects STRAIN:CHANNEL=NEW");
                   renames.emplace_back(v.substr(0, eq), v.substr(eq + 1));
               })
        .flag({"--json"}, [&] { json_out = true; })
        .positional([&](const std::string& v) { positional.push_back(v); })
        .parse(args);
    auto load_strains = [&](const std::vector<std::string>& ids) {
        StrainLibrary lib(library);
        std::vector<Strain> out;
        for (const auto& id : ids) out.push_back(lib.load(id));
        return out;
    };

    if (sub == "header") {
        if (positional.empty()) throw std::runtime_error("plugin header needs at least one strain");
        if (class_name.empty()) throw std::runtime_error("plugin header needs --class NAME");
        const std::string text = generate_base_header(load_strains(positional), class_name, renames);
        if (out_file.empty()) std::cout << text;
        else {
            write_new_file(out_file, text);
            std::printf("wrote %s\n", out_file.c_str());
        }
        return 0;
    }
    if (sub == "new") {
        if (positional.size() != 1) throw std::runtime_error("plugin new needs exactly one NAME");
        const std::string name = positional[0];
        if (class_name.empty()) class_name = class_name_for(name);
        std::filesystem::create_directories(dir);
        std::string header, base;
        if (!from.empty()) {
            base = class_name + "Base";
            header = name + "_base.hpp";
            write_new_file(std::filesystem::path(dir) / header, generate_base_header(load_strains(from), base, renames));
            std::printf("wrote %s/%s (base class %s from %zu strain(s))\n", dir.c_str(), header.c_str(), base.c_str(), from.size());
        }
        write_new_file(std::filesystem::path(dir) / (name + ".cpp"), plugin_template(name, class_name, header, base));
        std::printf("wrote %s/%s.cpp\nnext: edit it, then `adaptogen plugin validate %s/%s.cpp`\n", dir.c_str(), name.c_str(),
                    dir.c_str(), name.c_str());
        return 0;
    }
    if (positional.size() != 1) throw std::runtime_error("plugin " + sub + " needs exactly one FILE");
    const std::filesystem::path file = positional[0];
    if (sub == "build") {
        const BuildResult b = build_plugin(file);
        if (json_out) {
            std::cout << json{{"ok", b.ok}, {"library", b.library.string()}, {"cached", b.cached}, {"command", b.command},
                              {"diagnostics", diagnostics_json(b)}, {"output", b.output}}.dump(2) << '\n';
        } else if (b.ok) {
            std::printf("%s %s\n", b.cached ? "up to date:" : "built", b.library.string().c_str());
        } else {
            std::printf("build failed:\n%s", b.output.c_str());
        }
        return b.ok ? 0 : 1;
    }
    if (sub == "validate") {
        const ValidationReport r = validate_plugin(file, params, 1, library);
        if (json_out) {
            std::cout << json{{"ok", r.ok}, {"errors", r.errors}, {"warnings", r.warnings}, {"notes", r.notes},
                              {"info", r.info}, {"diagnostics", diagnostics_json(r.build)}}.dump(2) << '\n';
        } else {
            for (const auto& e : r.errors) std::printf("error: %s\n", e.c_str());
            for (const auto& w : r.warnings) std::printf("warning: %s\n", w.c_str());
            for (const auto& n : r.notes) std::printf("note: %s\n", n.c_str());
            std::printf("%s\n", r.ok ? "plug-in OK" : "plug-in has errors");
        }
        return r.ok ? 0 : 1;
    }
    if (sub == "info") {
        auto p = make_plugin_problem(file.string(), params, 1);
        std::cout << p->info().dump(2) << '\n';
        return 0;
    }
    throw std::runtime_error("unknown plugin subcommand '" + sub + "'");
}

}  // namespace adaptogen::cli
