// adaptogen command-line interface: the command table. Each command lives in its own file.
#include <filesystem>
#include <iostream>
#include <map>

#include "adaptogen/mcp.hpp"
#include "adaptogen/registry.hpp"
#include "common.hpp"

using namespace adaptogen;
using namespace adaptogen::cli;

namespace {

const char* kUsage = R"(adaptogen - evolve programs that solve problems

usage:
  adaptogen list                              list problems and their parameters
  adaptogen config                            print the default engine config as JSON
  adaptogen evolve --problem NAME [options]   evolve a program
  adaptogen show --program FILE [--all]       show a saved program and its scores
  adaptogen strains list|show|check|reindex   browse and inspect the strain bank (see below)
  adaptogen plugin new|header|build|validate|info   write problems as C++ plug-ins (see below)
  adaptogen decide STRAIN --options JSON|@FILE [--context JSON|@FILE]
                                              apply a strain: choice, scores, confidence (JSON)
  adaptogen adapt STRAIN --data NEW.csv [--only-new] [--generations N] [--force]
                                              re-evolve a data-driven strain on old + new data (or only the
                                              new data: specialise it to your use case), starting from it;
                                              saved as the next version only if not worse on held-out
                                              cases (--force saves anyway)
  adaptogen mcp [--workspace DIR] [--library DIR]   run as an MCP server (tools for Claude) on stdio
  adaptogen export (--program FILE | --strain ID) [--language cpp|python|javascript] [--name NAME] [-o FILE]
                                              plug-in code for apps: exact run(), choose(options) and
                                              a report card against simple solutions

evolve options:
  --params JSON          problem parameters, e.g. '{"target":"koza1"}'
  --param KEY=VALUE      set one problem parameter (repeatable)
  --config FILE          engine config JSON (see `adaptogen config`)
  --set KEY=VALUE        override one config key (repeatable), e.g. --set population=1000
  --seed N  --population N  --generations N  --threads N
  --selection tournament|lexicase
  --out FILE             save the result (program + problem + config) as JSON
  --csv FILE             write per-generation statistics as CSV
  --json                 machine-readable output: one JSON event per line
  --quiet                only print the final result
  --all                  also show non-effective code (introns)
  --lineage FILE.md      write how the solution evolved: every improvement, what produced it
                         and the code change
  --no-simplify          keep the raw evolved program (default: remove code that does not
                         change any training case's score, verified case by case)

strain bank (saved programs that can be revived on new problems):
  --save-strain NAME     save the result as strain NAME-v<next version>
  --description TEXT     description stored with the saved strain
  --seed-strain ID       start from a saved strain (repeatable: several parents)
  --seed-random F        fraction of the population that starts random (default 0.1)
  --retain W             keep scoring on the parent strains' problems with weight W (0..1)
  --map OLD=NEW          map a strain channel name to a problem channel name (repeatable)
  --library DIR          strain library folder (default: $ADAPTOGEN_LIBRARY or ./strains)

  adaptogen strains list [--library DIR]
  adaptogen strains show ID [--all]                     interface, behaviour and lineage
  adaptogen strains check ID --problem NAME [--params JSON] [--param K=V] [--map OLD=NEW]
  adaptogen strains simplify ID                         save a simplified new version (verified)
  adaptogen strains lineage ID [-o FILE.md]             ancestry report: problems, scores, code changes

plug-ins (new problems in C++, run in isolated worker processes; use as --problem plugin:FILE.cpp):
  adaptogen plugin new NAME [--from STRAIN]... [--rename STRAIN:CHANNEL=NEW]... [--dir DIR]
                                   write NAME.cpp (and NAME_base.hpp when deriving from strains)
  adaptogen plugin header STRAIN... --class NAME [--rename STRAIN:CHANNEL=NEW]... [-o FILE]
                                   generate a C++ base class from strains (multiple inheritance)
  adaptogen plugin build FILE.cpp [--json]       compile (cached); structured diagnostics
  adaptogen plugin validate FILE [--params JSON] [--param K=V] [--json]
                                   build, load and check interface, inheritance and behaviour
  adaptogen plugin info FILE [--params JSON]     print the plug-in's interface
  evolve --seed-parents            start from the plug-in's parent strains (with its channel maps)
  adaptogen strains reindex [--library DIR]
)";


int cmd_mcp(const Args& args) {
    std::string workspace = ".", library = default_library();
    Options()
        .value({"--workspace"}, [&](const std::string& v) { workspace = v; })
        .value({"--library"}, [&](const std::string& v) { library = v; })
        .parse(args);
    if (!std::filesystem::is_directory(workspace)) throw std::runtime_error("workspace '" + workspace + "' is not a folder");
    McpServer server(workspace, std::filesystem::absolute(library));
    return server.serve(std::cin, std::cout);
}

const std::map<std::string, int (*)(const Args&)> kCommands = {
    {"list", cmd_list},       {"config", cmd_config}, {"evolve", cmd_evolve}, {"show", cmd_show},
    {"strains", cmd_strains}, {"plugin", cmd_plugin}, {"export", cmd_export}, {"decide", cmd_decide},
    {"adapt", cmd_adapt},     {"mcp", cmd_mcp},
};

}  // namespace

int main(int argc, char** argv) {
    register_builtin_problems();
    Args args(argv + 1, argv + argc);
    if (args.empty() || args[0] == "-h" || args[0] == "--help" || args[0] == "help") {
        std::cout << kUsage;
        return args.empty() ? 1 : 0;
    }
    const auto it = kCommands.find(args[0]);
    if (it == kCommands.end()) {
        std::cerr << "unknown command '" << args[0] << "'\n\n" << kUsage;
        return 1;
    }
    try {
        return it->second(Args(args.begin() + 1, args.end()));
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
        return 1;
    }
}
