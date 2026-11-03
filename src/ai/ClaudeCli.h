// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "CodexAgent.h"

// CoProducer chat through the locally installed Claude Code CLI, so a Claude
// subscription (Pro/Max, signed in with `claude auth login`) is used instead of a
// separately billed API key. VibeRa runs `claude -p --output-format stream-json`
// in a VibeRa-owned folder with every built-in tool disabled; only the `vibera`
// MCP server (the loopback bridge) is loaded and pre-approved. Follow-ups resume
// the same session.
namespace vibera::ai
{
struct ClaudeCliStatus
{
    bool installed = false, loggedIn = false;
    juce::String executable, version, authMethod, subscription; // From `claude auth status` (no email kept).
};
ClaudeCliStatus detectClaudeCli(); // Never reads credential files.
ClaudeCliStatus cachedClaudeCli(bool refresh = false);
std::optional<ClaudeCliStatus> knownClaudeCli(); // Never detects: safe on the message thread.

// Model choices: "" (the CLI default) and the aliases the CLI resolves to the latest models.
const juce::StringArray &claudeCliModels();
// Effort levels the CLI accepts (low..max); anything else means the default.
juce::String claudeCliEffort(const juce::String &effort);

// Maps one stream-json event to transcript events; updates threadId (session_id).
void parseClaudeEvent(const juce::var &event, std::vector<AgentEvent> &out, juce::String &threadId);

struct ClaudeRun
{
    juce::String prompt, model, effort;
    juce::File bridgeExecutable, workFolder;
    int bridgePort = 0;
    juce::String bridgeToken;
};
juce::StringArray claudeArguments(const ClaudeCliStatus &, const ClaudeRun &, const juce::String &resumeThread);
CliTurn claudeTurn(const ClaudeCliStatus &, const ClaudeRun &, bool mayEditProject);
} // namespace vibera::ai
