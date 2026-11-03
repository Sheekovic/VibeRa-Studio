// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "LlmClient.h"
#include <atomic>
#include <mutex>
#include <thread>

// "AI Producer": a chat agent that drives VibeRa through its MCP tools (notes,
// patterns, mixer, plugins, export...) plus local music generation. The model
// call and loop run on an owned worker thread; tool execution is delegated to
// an executor supplied by Studio, which runs canonical MCP operations on the
// message thread. Conversation history is append-only.
namespace vibera::ai
{
struct AgentEvent
{
    enum class Kind
    {
        user,
        assistant,
        tool,
        error,
        status
    } kind;
    juce::String text;
};

struct ToolOutcome
{
    juce::String content;
    bool isError = false;
};

// Called on the agent thread. Must honour `cancelled` while waiting.
using ToolExecutor =
    std::function<ToolOutcome(const juce::String &name, const juce::var &input, const CancelCheck &cancelled)>;

juce::String producerSystemPrompt(bool mayEditProject);
// Tool result text kept for the model: oversized readbacks are truncated with a note.
juce::String clampToolResult(const juce::String &, int limit = 24000);
// JSON tool result for an agent: unchanged when it fits; otherwise every top-level scalar
// (tempo, meter, revision...) is kept first and only the largest sections are replaced by a
// note naming the narrower readback, so key values never fall behind a truncation point.
juce::String agentToolText(const juce::var &data, int limit = 20000);

class ProducerAgent
{
  public:
    ProducerAgent() = default;
    ~ProducerAgent();
    ProducerAgent(const ProducerAgent &) = delete;
    ProducerAgent &operator=(const ProducerAgent &) = delete;

    juce::Result send(const juce::String &userText, const LlmConfig &, std::vector<ToolSpec> tools,
                      ToolExecutor executor, int maxSteps, bool mayEditProject);
    void cancel() noexcept
    {
        cancelled.store(true);
    }
    void reset(); // New conversation (only while idle).
    bool busy() const
    {
        return running.load();
    }
    std::vector<AgentEvent> events() const;
    size_t eventCount() const;

  private:
    mutable std::mutex mutex;
    std::vector<AgentEvent> log;
    Conversation conversation;
    juce::String conversationKey; // dialect+base+model; a change starts a fresh conversation.
    std::atomic<bool> cancelled{false}, running{false};
    std::thread worker;
    void add(AgentEvent::Kind, const juce::String &);
    void join();
};
} // namespace vibera::ai
