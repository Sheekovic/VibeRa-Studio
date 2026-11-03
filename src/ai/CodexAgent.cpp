// SPDX-License-Identifier: AGPL-3.0-or-later
#include "CodexAgent.h"
#include "WorkerProcess.h"
#include <algorithm>
#include <optional>

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
// TOML literal string: no escapes and no double quotes, so it survives cmd/npm-shim quoting.
juce::String tomlString(const juce::String &value)
{
    return "'" + value.removeCharacters("'\"") + "'";
}
} // namespace

CodexStatus detectCodex()
{
    CodexStatus status;
    // Tests point this at a fake CLI; normal runs resolve the npm codex.cmd shim like a user shell.
    const auto overridePath = juce::SystemStats::getEnvironmentVariable("VIBERA_CODEX_EXECUTABLE", {});
    const auto where = overridePath.isNotEmpty() ? overridePath
                                                 : runAndRead({"cmd.exe", "/d", "/c", "where codex"}, 10000);
    for (const auto &line : juce::StringArray::fromLines(where))
        if (line.endsWithIgnoreCase(".cmd") || line.endsWithIgnoreCase(".exe"))
        {
            status.executable = line.trim();
            break;
        }
    if (status.executable.isEmpty())
        return status;
    status.installed = true;
    // ChildProcess quotes arguments itself; adding quotes here would double them for cmd.
    status.version = runAndRead({"cmd.exe", "/d", "/c", status.executable, "--version"}, 20000);
    status.login = runAndRead({"cmd.exe", "/d", "/c", status.executable, "login", "status"}, 20000)
                       .upToFirstOccurrenceOf("\n", false, false);
    status.loggedIn = status.login.containsIgnoreCase("logged in") && !status.login.containsIgnoreCase("not logged");
    status.chatgpt = status.loggedIn && status.login.containsIgnoreCase("chatgpt");
    if (status.loggedIn)
        status.models = parseCodexModels(
            juce::JSON::parse(runAndRead({"cmd.exe", "/d", "/c", status.executable, "debug", "models"}, 20000)));
    return status;
}

const CodexModel *CodexStatus::find(const juce::String &slug) const
{
    for (const auto &model : models)
        if (model.slug == slug)
            return &model;
    return nullptr;
}

std::vector<CodexModel> parseCodexModels(const juce::var &catalog)
{
    std::vector<std::pair<int, CodexModel>> listed;
    if (const auto *rows = catalog["models"].getArray())
        for (const auto &row : *rows)
        {
            if (row["visibility"].toString() != "list" || row["slug"].toString().isEmpty())
                continue;
            CodexModel model;
            model.slug = row["slug"].toString();
            model.name = row["display_name"].toString().isNotEmpty() ? row["display_name"].toString() : model.slug;
            model.description = row["description"].toString();
            model.defaultEffort = row["default_reasoning_level"].toString();
            if (const auto *levels = row["supported_reasoning_levels"].getArray())
                for (const auto &level : *levels)
                    if (level["effort"].isString())
                        model.efforts.add(level["effort"].toString());
            listed.push_back({int(row["priority"]), std::move(model)});
        }
    std::stable_sort(listed.begin(), listed.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
    std::vector<CodexModel> models;
    for (auto &entry : listed)
        models.push_back(std::move(entry.second));
    return models;
}

juce::String codexEffort(const CodexStatus &codex, const juce::String &model, const juce::String &effort)
{
    if (effort.isEmpty() || codex.models.empty())
        return effort;
    if (const auto *chosen = codex.find(model))
        return chosen->efforts.contains(effort) ? effort : juce::String{};
    for (const auto &listed : codex.models)
        if (!listed.efforts.contains(effort))
            return {};
    return effort;
}

namespace
{
std::mutex codexGuard;
std::optional<CodexStatus> codexStatus;
} // namespace

std::optional<CodexStatus> knownCodex()
{
    std::lock_guard<std::mutex> lock(codexGuard);
    return codexStatus;
}

CodexStatus cachedCodex(bool refresh)
{
    auto &guard = codexGuard;
    auto &status = codexStatus;
    if (refresh || !knownCodex())
    {
        auto detected = detectCodex(); // Slow (spawns codex); never hold the lock while running it.
        std::lock_guard<std::mutex> lock(guard);
        status = std::move(detected);
    }
    std::lock_guard<std::mutex> lock(guard);
    return *status;
}

void parseCodexEvent(const juce::var &event, std::vector<AgentEvent> &out, juce::String &threadId)
{
    const auto type = event["type"].toString();
    if (type == "thread.started")
    {
        if (event["thread_id"].isString())
            threadId = event["thread_id"].toString();
        return;
    }
    if (type == "turn.failed")
    {
        out.push_back({AgentEvent::Kind::error, "Codex: " + event["error"]["message"].toString()});
        return;
    }
    if (type == "error")
    {
        out.push_back({AgentEvent::Kind::error, "Codex: " + event["message"].toString()});
        return;
    }
    if (!type.startsWith("item."))
        return;
    const auto item = event["item"];
    const auto kind = item["type"].toString();
    if (kind == "agent_message" && type == "item.completed" && item["text"].toString().trim().isNotEmpty())
        out.push_back({AgentEvent::Kind::assistant, item["text"].toString().trim()});
    else if (kind == "mcp_tool_call" && type == "item.started")
        out.push_back({AgentEvent::Kind::tool, item["tool"].toString() + " " +
                                                   juce::JSON::toString(item["arguments"], true).substring(0, 400)});
    else if (kind == "mcp_tool_call" && type == "item.completed" && item["status"].toString() == "failed")
        out.push_back({AgentEvent::Kind::tool, "  -> error: " + juce::JSON::toString(item["error"], true).substring(0, 300)});
    else if (kind == "command_execution" && type == "item.started")
        out.push_back({AgentEvent::Kind::tool, "shell: " + item["command"].toString().substring(0, 300)});
    else if (kind == "error")
        out.push_back({AgentEvent::Kind::error, "Codex: " + item["message"].toString()});
}

juce::StringArray codexArguments(const CodexStatus &codex, const CodexRun &run, const juce::String &resumeThread)
{
    juce::StringArray args{codex.executable, "exec"};
    if (resumeThread.isNotEmpty())
        args.addArray({"resume", resumeThread});
    args.addArray({"--json", "--skip-git-repo-check"});
    // Codex edits nothing on disk and runs no unsandboxed commands; it acts only through VibeRa's tools.
    args.addArray({"-c", "sandbox_mode='read-only'", "-c", "approval_policy='never'"});
    args.addArray({"-c", "mcp_servers.vibera.command=" + tomlString(run.bridgeExecutable.getFullPathName()), "-c",
                   "mcp_servers.vibera.args=['--mcp-bridge', '" + juce::String(run.bridgePort) + "', '" +
                       run.bridgeToken + "']",
                   "-c", "mcp_servers.vibera.tool_timeout_sec=3600", "-c", "mcp_servers.vibera.startup_timeout_sec=30",
                   // Only VibeRa's own tools are pre-approved (non-interactive exec cannot prompt);
                   // VibeRa still enforces its read-only setting on every call.
                   "-c", "mcp_servers.vibera.default_tools_approval_mode='approve'"});
    if (run.model.isNotEmpty())
        args.addArray({"-c", "model=" + tomlString(run.model)});
    if (run.effort.isNotEmpty())
        args.addArray({"-c", "model_reasoning_effort=" + tomlString(run.effort)});
    if (resumeThread.isEmpty())
        args.addArray({"-C", run.workFolder.getFullPathName()});
    args.add("-"); // Prompt from stdin: avoids command-line length and quoting limits.
    return args;
}

CliTurn codexTurn(const CodexStatus &codex, const CodexRun &run, bool mayEditProject)
{
    CliTurn turn;
    turn.name = "Codex";
    turn.workFolder = run.workFolder;
    // The first turn carries the producer brief; resumed turns already have it in the thread.
    turn.firstPrompt = producerSystemPrompt(mayEditProject) +
                       "\nUse only the `vibera` MCP tools to work on the open project.\n\nUser request:\n" +
                       run.prompt;
    turn.resumedPrompt = run.prompt;
    turn.arguments = [codex, run](const juce::String &resume) { return codexArguments(codex, run, resume); };
    turn.parse = parseCodexEvent;
    return turn;
}

CliAgent::~CliAgent()
{
    cancel();
    join();
}

void CliAgent::join()
{
    if (worker.joinable())
        worker.join();
}

void CliAgent::add(AgentEvent::Kind kind, const juce::String &text)
{
    std::lock_guard<std::mutex> lock(mutex);
    log.push_back({kind, text});
}

std::vector<AgentEvent> CliAgent::events() const
{
    std::lock_guard<std::mutex> lock(mutex);
    return log;
}

size_t CliAgent::eventCount() const
{
    std::lock_guard<std::mutex> lock(mutex);
    return log.size();
}

void CliAgent::reset()
{
    if (busy())
        return;
    join();
    std::lock_guard<std::mutex> lock(mutex);
    log.clear();
    threadId.clear();
}

juce::Result CliAgent::send(const juce::String &userText, CliTurn turn)
{
    if (userText.trim().isEmpty())
        return juce::Result::fail("ai.emptyMessage: Type a message first");
    if (busy())
        return juce::Result::fail("ai.busy: The producer is still working; Stop it first");
    join();
    juce::String resume;
    {
        std::lock_guard<std::mutex> lock(mutex);
        resume = threadId;
        log.push_back({AgentEvent::Kind::user, userText});
    }
    cancelled.store(false);
    running.store(true);
    worker = std::thread(
        [this, turn, resume]
        {
            turn.workFolder.createDirectory();
            const auto stamp = juce::String(juce::Time::currentTimeMillis());
            const auto promptFile = turn.workFolder.getChildFile("prompt-" + stamp + ".txt");
            const auto events = turn.workFolder.getChildFile("events-" + stamp + ".jsonl");
            const auto errors = turn.workFolder.getChildFile("stderr-" + stamp + ".log");
            promptFile.replaceWithText(resume.isEmpty() ? turn.firstPrompt : turn.resumedPrompt);
            juce::Array<juce::File> extra;
            for (const auto &[name, text] : turn.files)
            {
                extra.add(turn.workFolder.getChildFile(name));
                extra.getLast().replaceWithText(text);
            }
            juce::String line;
            for (const auto &part : turn.arguments(resume))
                line << (line.isEmpty() ? "call " : " ")
                     << (part.isEmpty() || part.containsAnyOf(" '[]=,*") ? part.quoted() : part);
            juce::String environment;
            for (const auto &key : turn.environment.getAllKeys())
                environment << "set \"" << key << "=" << turn.environment[key] << "\"\r\n";
            const auto script = turn.workFolder.getChildFile("run-" + stamp + ".cmd");
            script.replaceWithText("@echo off\r\ncd /d " + turn.workFolder.getFullPathName().quoted() + "\r\n" +
                                   environment + line + " < " + promptFile.getFullPathName().quoted() + " > " +
                                   events.getFullPathName().quoted() + " 2> " + errors.getFullPathName().quoted() +
                                   "\r\nexit /b %errorlevel%\r\n");
            add(AgentEvent::Kind::status, turn.name + " is working...");
            WorkerProcess process;
            WorkerLimits limits;
            limits.containDescendants = true;
            auto cleanUp = [&]
            {
                promptFile.deleteFile();
                script.deleteFile();
                for (const auto &file : extra)
                    file.deleteFile(); // May hold the per-session bridge token.
            };
            if (!process.start({"cmd.exe", "/d", "/c", script.getFullPathName()}, 0, limits))
            {
                add(AgentEvent::Kind::error, "Could not start " + turn.name);
                cleanUp();
                running.store(false);
                return;
            }
            juce::int64 offset = 0;
            std::string partial; // Raw bytes: decode only complete lines so UTF-8 never splits.
            juce::String thread = resume;
            auto drain = [&]
            {
                if (!events.existsAsFile() || events.getSize() <= offset)
                    return;
                juce::FileInputStream in(events);
                if (!in.openedOk())
                    return;
                in.setPosition(offset);
                juce::MemoryBlock bytes;
                in.readIntoMemoryBlock(bytes);
                offset = in.getPosition();
                partial.append(static_cast<const char *>(bytes.getData()), bytes.getSize());
                for (auto newline = partial.find('\n'); newline != std::string::npos; newline = partial.find('\n'))
                {
                    const auto row = juce::String::fromUTF8(partial.data(), int(newline)).trim();
                    partial.erase(0, newline + 1);
                    if (row.isEmpty())
                        continue;
                    std::vector<AgentEvent> parsed;
                    turn.parse(juce::JSON::parse(row), parsed, thread);
                    std::lock_guard<std::mutex> lock(mutex);
                    log.insert(log.end(), parsed.begin(), parsed.end());
                }
            };
            while (process.isRunning())
            {
                if (cancelled.load())
                {
                    process.kill(); // Owned job object: ends the CLI and the bridge it started.
                    add(AgentEvent::Kind::status, "Stopped.");
                    break;
                }
                drain();
                juce::Thread::sleep(150);
            }
            drain();
            if (!cancelled.load() && process.getExitCode() != 0)
            {
                const auto detail = errors.loadFileAsString().trim().getLastCharacters(600);
                add(AgentEvent::Kind::error, turn.name + " exited with code " + juce::String(process.getExitCode()) +
                                                 (detail.isNotEmpty() ? ": " + detail : juce::String{}));
            }
            {
                std::lock_guard<std::mutex> lock(mutex);
                threadId = thread;
            }
            cleanUp();
            running.store(false);
        });
    return juce::Result::ok();
}
} // namespace vibera::ai
