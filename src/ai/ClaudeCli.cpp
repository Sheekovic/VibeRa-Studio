// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ClaudeCli.h"

namespace vibera::ai
{
namespace
{
juce::String runAndRead(const juce::StringArray &command, int timeoutMs)
{
    juce::ChildProcess process;
    if (!process.start(command))
        return {};
    const auto output = process.readAllProcessOutput();
    process.waitForProcessToFinish(timeoutMs);
    return output.trim();
}
std::mutex claudeGuard;
std::optional<ClaudeCliStatus> claudeStatus;
const juce::String mcpConfigName = "vibera-mcp.json";
} // namespace

ClaudeCliStatus detectClaudeCli()
{
    ClaudeCliStatus status;
    // Tests point this at a fake CLI; normal runs resolve the npm claude.cmd shim like a user shell.
    const auto overridePath = juce::SystemStats::getEnvironmentVariable("VIBERA_CLAUDE_EXECUTABLE", {});
    const auto where = overridePath.isNotEmpty() ? overridePath
                                                 : runAndRead({"cmd.exe", "/d", "/c", "where claude"}, 10000);
    for (const auto &line : juce::StringArray::fromLines(where))
        if (line.endsWithIgnoreCase(".cmd") || line.endsWithIgnoreCase(".exe"))
        {
            status.executable = line.trim();
            break;
        }
    if (status.executable.isEmpty() || !juce::File(status.executable).existsAsFile())
        return status;
    status.installed = true;
    status.version = runAndRead({"cmd.exe", "/d", "/c", status.executable, "--version"}, 20000);
    const auto auth =
        juce::JSON::parse(runAndRead({"cmd.exe", "/d", "/c", status.executable, "auth", "status"}, 20000));
    status.loggedIn = bool(auth.getProperty("loggedIn", false));
    status.authMethod = auth["authMethod"].toString();
    status.subscription = auth["subscriptionType"].toString();
    return status;
}

std::optional<ClaudeCliStatus> knownClaudeCli()
{
    std::lock_guard<std::mutex> lock(claudeGuard);
    return claudeStatus;
}

ClaudeCliStatus cachedClaudeCli(bool refresh)
{
    if (refresh || !knownClaudeCli())
    {
        auto detected = detectClaudeCli(); // Slow (spawns claude); never hold the lock while running it.
        std::lock_guard<std::mutex> lock(claudeGuard);
        claudeStatus = std::move(detected);
    }
    std::lock_guard<std::mutex> lock(claudeGuard);
    return *claudeStatus;
}

const juce::StringArray &claudeCliModels()
{
    static const juce::StringArray models{"", "opus", "sonnet", "haiku", "fable"};
    return models;
}

juce::String claudeCliEffort(const juce::String &effort)
{
    return juce::StringArray{"low", "medium", "high", "xhigh", "max"}.contains(effort) ? effort : juce::String{};
}

void parseClaudeEvent(const juce::var &event, std::vector<AgentEvent> &out, juce::String &threadId)
{
    if (event["session_id"].isString() && event["session_id"].toString().isNotEmpty())
        threadId = event["session_id"].toString();
    const auto type = event["type"].toString();
    const auto *content = event["message"]["content"].getArray();
    if (type == "assistant" && content)
    {
        for (const auto &block : *content)
        {
            const auto kind = block["type"].toString();
            if (kind == "text" && block["text"].toString().trim().isNotEmpty())
                out.push_back({AgentEvent::Kind::assistant, block["text"].toString().trim()});
            else if (kind == "tool_use")
                out.push_back({AgentEvent::Kind::tool,
                               block["name"].toString().replace("mcp__vibera__", "") + " " +
                                   juce::JSON::toString(block["input"], true).substring(0, 400)});
        }
    }
    else if (type == "user" && content)
    {
        for (const auto &block : *content)
            if (block["type"].toString() == "tool_result" && bool(block.getProperty("is_error", false)))
            {
                auto text = block["content"].isString() ? block["content"].toString()
                                                        : juce::JSON::toString(block["content"], true);
                out.push_back({AgentEvent::Kind::tool, "  -> error: " + text.substring(0, 300)});
            }
    }
    else if (type == "result" && (bool(event.getProperty("is_error", false)) ||
                                  (event["subtype"].isString() && event["subtype"].toString() != "success")))
    {
        const auto detail = event["result"].toString().isNotEmpty() ? event["result"].toString()
                                                                    : event["subtype"].toString();
        out.push_back({AgentEvent::Kind::error, "Claude: " + detail.substring(0, 600)});
    }
}

juce::StringArray claudeArguments(const ClaudeCliStatus &claude, const ClaudeRun &run, const juce::String &resume)
{
    juce::StringArray args{claude.executable, "-p", "--output-format", "stream-json", "--verbose"};
    // No built-in tools (shell, files, web); only VibeRa's MCP server, pre-approved. dontAsk denies
    // anything else without prompting. VibeRa still enforces its read-only setting on every call.
    args.addArray({"--strict-mcp-config", "--mcp-config", mcpConfigName, "--tools", "", "--allowedTools",
                   "mcp__vibera", "--permission-mode", "dontAsk"});
    if (run.model.isNotEmpty())
        args.addArray({"--model", run.model});
    if (const auto effort = claudeCliEffort(run.effort); effort.isNotEmpty())
        args.addArray({"--effort", effort});
    if (resume.isNotEmpty())
        args.addArray({"--resume", resume});
    return args;
}

CliTurn claudeTurn(const ClaudeCliStatus &claude, const ClaudeRun &run, bool mayEditProject)
{
    CliTurn turn;
    turn.name = "Claude";
    turn.workFolder = run.workFolder;
    turn.firstPrompt = producerSystemPrompt(mayEditProject) +
                       "\nUse only the `vibera` MCP tools to work on the open project.\n\nUser request:\n" +
                       run.prompt;
    turn.resumedPrompt = run.prompt;
    turn.arguments = [claude, run](const juce::String &resume) { return claudeArguments(claude, run, resume); };
    turn.parse = parseClaudeEvent;
    auto server = juce::DynamicObject::Ptr(new juce::DynamicObject);
    server->setProperty("type", "stdio");
    server->setProperty("command", run.bridgeExecutable.getFullPathName());
    server->setProperty("args", juce::Array<juce::var>{"--mcp-bridge", juce::String(run.bridgePort), run.bridgeToken});
    auto servers = juce::DynamicObject::Ptr(new juce::DynamicObject);
    servers->setProperty("vibera", juce::var(server.get()));
    auto config = juce::DynamicObject::Ptr(new juce::DynamicObject);
    config->setProperty("mcpServers", juce::var(servers.get()));
    turn.files.push_back({mcpConfigName, juce::JSON::toString(juce::var(config.get()))});
    // Long jobs (music generation) run inside one tool call.
    turn.environment.set("MCP_TOOL_TIMEOUT", "3600000");
    turn.environment.set("MCP_TIMEOUT", "30000");
    return turn;
}
} // namespace vibera::ai
