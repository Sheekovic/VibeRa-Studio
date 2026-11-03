// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "ProducerAgent.h"
#include <functional>
#include <optional>

// CoProducer chat backed by the locally installed Codex CLI, so a ChatGPT
// subscription (Plus/Pro, signed in with `codex login`) is used instead of a
// separately billed API key. VibeRa runs `codex exec --json` in a VibeRa-owned,
// read-only working folder and gives Codex its tools through the loopback MCP
// bridge; follow-up messages continue the same Codex thread with `resume`.
namespace vibera::ai
{
struct CodexModel
{
    juce::String slug, name, description, defaultEffort;
    juce::StringArray efforts; // Reasoning levels this model accepts (model_reasoning_effort).
};
struct CodexStatus
{
    bool installed = false, loggedIn = false, chatgpt = false;
    juce::String executable, version, login; // login: the `codex login status` line.
    std::vector<CodexModel> models;          // User-visible catalog from `codex debug models`.
    const CodexModel *find(const juce::String &slug) const;
};
// Detects `codex` on PATH, its sign-in method and model catalog (never reads credential files).
CodexStatus detectCodex();
CodexStatus cachedCodex(bool refresh = false); // Thread-safe copy; detects once unless refreshed.
std::optional<CodexStatus> knownCodex();         // Never detects: safe on the message thread.
// Listed (visibility "list") models from the `codex debug models` JSON, in catalog priority order.
std::vector<CodexModel> parseCodexModels(const juce::var &catalog);
// The effort to request: empty (model default) unless the chosen model, or every listed model
// when Codex picks its own default, accepts it.
juce::String codexEffort(const CodexStatus &, const juce::String &model, const juce::String &effort);

// Maps one `codex exec --json` JSONL event to transcript events; updates threadId.
void parseCodexEvent(const juce::var &event, std::vector<AgentEvent> &out, juce::String &threadId);

struct CodexRun
{
    juce::String prompt, model; // Empty model: Codex default from ~/.codex/config.toml.
    juce::String effort;        // Empty: the model's default reasoning effort.
    juce::File bridgeExecutable, workFolder;
    int bridgePort = 0;
    juce::String bridgeToken;
};
// Full argument list for one turn (exposed for tests).
juce::StringArray codexArguments(const CodexStatus &, const CodexRun &, const juce::String &resumeThread);

// One non-interactive turn of a local agent CLI (Codex, Claude Code): how to build the
// command line, how to read its JSONL output, and the files/environment it needs.
struct CliTurn
{
    juce::String name;                       // "Codex", "Claude": prefixes errors and status.
    juce::String firstPrompt, resumedPrompt; // stdin for a new thread / a resumed one.
    juce::File workFolder;
    std::function<juce::StringArray(const juce::String &resumeThread)> arguments;
    std::function<void(const juce::var &, std::vector<AgentEvent> &, juce::String &threadId)> parse;
    juce::StringPairArray environment;                        // `set` lines in the launcher script.
    std::vector<std::pair<juce::String, juce::String>> files; // name -> text in workFolder; removed after.
};
// Codex turn for this run (bridge, sandbox and approval policy, model, effort).
CliTurn codexTurn(const CodexStatus &, const CodexRun &, bool mayEditProject);

// Runs CLI turns on a worker thread inside an owned job object, tailing their JSONL
// output into transcript events; follow-ups resume the thread the CLI reported.
class CliAgent
{
  public:
    CliAgent() = default;
    ~CliAgent();
    CliAgent(const CliAgent &) = delete;
    CliAgent &operator=(const CliAgent &) = delete;

    juce::Result send(const juce::String &userText, CliTurn turn);
    void cancel() noexcept
    {
        cancelled.store(true);
    }
    void reset();
    bool busy() const
    {
        return running.load();
    }
    std::vector<AgentEvent> events() const;
    size_t eventCount() const;

  private:
    mutable std::mutex mutex;
    std::vector<AgentEvent> log;
    juce::String threadId;
    std::atomic<bool> cancelled{false}, running{false};
    std::thread worker;
    void add(AgentEvent::Kind, const juce::String &);
    void join();
};
} // namespace vibera::ai
