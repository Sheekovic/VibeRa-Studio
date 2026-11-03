// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "AiHttp.h"
#include "AiSettings.h"
#include <vector>

// Provider-neutral chat-with-tools client. Conversation history is kept in the
// provider's native message shape and is append-only: assistant turns are stored
// exactly as returned (including any thinking blocks) and never edited.
namespace vibera::ai
{
struct LlmConfig
{
    Dialect dialect = Dialect::anthropic;
    juce::String baseUrl, model, apiKey;
    juce::String effort = "high";
    int maxTokens = 16000;
    int timeoutMs = 600000;
};

struct ToolSpec
{
    juce::String name, description;
    juce::var inputSchema; // JSON Schema object.
};

struct ToolCall
{
    juce::String id, name;
    juce::var input;
};

struct ToolResult
{
    juce::String id, content;
    bool isError = false;
};

struct LlmTurn
{
    juce::String error; // Non-empty: transport/API failure; nothing appended.
    juce::String text;
    std::vector<ToolCall> calls;
    bool refusal = false, truncated = false;
    juce::var assistantMessage; // Native message appended to history on success.
    bool ok() const noexcept
    {
        return error.isEmpty();
    }
};

class Conversation
{
  public:
    explicit Conversation(Dialect d = Dialect::anthropic) : dialect(d) {}
    Dialect dialect;
    juce::String system;
    juce::Array<juce::var> messages;
    void addUser(const juce::String &text);
    void addAssistant(const LlmTurn &turn);
    void addToolResults(const std::vector<ToolResult> &results);
};

// Request/response codecs (pure; unit-tested without network).
juce::var buildRequest(const LlmConfig &, const Conversation &, const std::vector<ToolSpec> &);
juce::StringPairArray requestHeaders(const LlmConfig &);
juce::String requestUrl(const LlmConfig &);
LlmTurn parseResponse(Dialect, const juce::var &json);

// One model call; blocking, worker thread only.
LlmTurn complete(const LlmConfig &, const Conversation &, const std::vector<ToolSpec> &, const CancelCheck & = {});
// Lists model ids (GET /v1/models or /models); also serves as the connection test.
juce::Result listModels(const LlmConfig &, juce::StringArray &models, const CancelCheck & = {});
} // namespace vibera::ai
