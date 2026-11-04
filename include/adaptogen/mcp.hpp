#pragma once
// MCP (Model Context Protocol) server: exposes adaptogen as tools for Claude and other AI clients.
#include <filesystem>
#include <iosfwd>
#include <memory>

#include <nlohmann/json.hpp>

namespace adaptogen {

class McpServer {
public:
    /// workspace: the only folder plug-in files may be written/read in; library: the strain bank.
    McpServer(std::filesystem::path workspace, std::filesystem::path library);
    ~McpServer();
    /// Handle one JSON-RPC message; returns the response (null for notifications).
    nlohmann::json handle(const nlohmann::json& message);
    /// Serve over a stream pair (stdio), one JSON message per line.
    int serve(std::istream& in, std::ostream& out);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace adaptogen
