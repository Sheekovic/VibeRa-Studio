// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "ProducerAgent.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

// Loopback MCP endpoint that lets a locally installed agent CLI (Codex, Claude
// Code) drive the running VibeRa through the AI Producer tools. Listens only on
// 127.0.0.1 with a per-session random token; `VibeRa Studio.exe --mcp-bridge
// <port> <token>` relays a CLI's stdio MCP connection to it. Tool calls run on
// the connection thread through the same executor as the in-app agent (which
// marshals canonical operations to the message thread), so long jobs such as
// music generation are not limited by the stdio server's 5 s dispatch bound.
namespace vibera::ai
{
class McpLoopbackHost
{
  public:
    McpLoopbackHost() = default;
    ~McpLoopbackHost();
    McpLoopbackHost(const McpLoopbackHost &) = delete;
    McpLoopbackHost &operator=(const McpLoopbackHost &) = delete;

    // (Re)starts with a fresh token and the given tool snapshot. Returns false when no port is free.
    bool start(std::vector<ToolSpec> tools, ToolExecutor executor, juce::String serverName = "VibeRa Studio");
    void stop();
    int port() const noexcept
    {
        return listeningPort.load();
    }
    juce::String token() const;

    // Pure protocol step (exposed for tests): one JSON-RPC request -> reply (void for notifications).
    struct Session
    {
        bool authenticated = false, negotiated = false, initialized = false;
    };
    juce::var handle(Session &, const juce::var &request, const CancelCheck &cancelled);

  private:
    mutable std::mutex mutex;
    std::vector<ToolSpec> tools;
    ToolExecutor executor;
    juce::String secret, name;
    std::unique_ptr<juce::StreamingSocket> listener;
    std::atomic<int> listeningPort{0};
    std::atomic<bool> stopping{false};
    std::thread acceptor;
    std::vector<std::thread> connections;
    std::vector<std::shared_ptr<juce::StreamingSocket>> sockets;
    void serve(std::shared_ptr<juce::StreamingSocket>);
};

// Relay for `--mcp-bridge <port> <token>`: stdin lines -> loopback, replies -> stdout.
int runMcpBridge(int port, const juce::String &token);
} // namespace vibera::ai
