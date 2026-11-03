// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ProducerAgent.h"
#include <algorithm>

namespace vibera::ai
{
juce::String producerSystemPrompt(bool mayEditProject)
{
    juce::String prompt =
        "You are CoProducer, the AI producer inside VibeRa Studio, a Windows DAW with an FL Studio-style workflow "
        "(Channel Rack, Patterns, Piano Roll, Playlist, Mixer). You work like a full music producer: you "
        "plan songs, write chord progressions, melodies, basslines and drum patterns as real editable notes, "
        "write lyrics and style prompts, arrange Patterns in the Playlist, set tempo, mix levels/pan/effects "
        "and generate audio with local models when useful.\n\n"
        "How to work:\n"
        "- Inspect before editing: vibera_state and vibera_inspect give stable IDs (channels, patterns, notes, "
        "mixer inserts). Never invent IDs.\n"
        "- Discover exact operations with vibera_commands (includeSchemas=true for one command) and apply "
        "edits with vibera_command / vibera_batch. Times are quarter-note beats; pitches are MIDI numbers "
        "(60 = C5 in VibeRa naming). Each command is one Undo step for the user.\n"
        "- vibera_generate_music renders a full audio track (instrumental or with vocals/lyrics) with "
        "ACE-Step 1.5 or YuE2 and places it as a Playlist audio clip. Use it for vocals, textures or a "
        "complete demo; prefer editable notes for parts the user wants to tweak.\n"
        "- Lyrics use section tags like [Verse], [Chorus], [Bridge]. Style prompts list genre, mood, "
        "instruments, vocal type, tempo.\n"
        "- Work in small verified steps, read back results, and fix mistakes. If a tool returns an error, "
        "read it and adjust instead of repeating the same call.\n"
        "- Keep replies short: say what you did and what the user can try next. Reply in the user's language.\n";
    if (!mayEditProject)
        prompt << "\nThe user has set read-only mode: you may inspect the project and give advice, lyrics and "
                  "prompts, but do not modify the project or generate audio.\n";
    return prompt;
}

juce::String clampToolResult(const juce::String &text, int limit)
{
    if (text.length() <= limit)
        return text;
    return text.substring(0, limit) + "\n[... truncated " + juce::String(text.length() - limit) +
           " characters; request a narrower readback (vibera_inspect with offset/limit) if needed]";
}

juce::String agentToolText(const juce::var &data, int limit)
{
    const auto full = juce::JSON::toString(data, true);
    const auto *object = data.getDynamicObject();
    if (full.length() <= limit || object == nullptr)
        return clampToolResult(full, limit);
    auto compact = juce::DynamicObject::Ptr(new juce::DynamicObject);
    std::vector<std::pair<juce::Identifier, juce::String>> sections;
    int used = 0;
    for (const auto &property : object->getProperties())
        if (property.value.isObject() || property.value.isArray())
            sections.push_back({property.name, juce::JSON::toString(property.value, true)});
        else
        {
            compact->setProperty(property.name, property.value);
            used += property.name.toString().length() + juce::JSON::toString(property.value).length() + 4;
        }
    // Smallest sections first, so as many whole sections as possible fit.
    std::stable_sort(sections.begin(), sections.end(),
                     [](const auto &a, const auto &b) { return a.second.length() < b.second.length(); });
    juce::StringArray omitted;
    for (const auto &[name, text] : sections)
        if (used + text.length() + 80 * (int(sections.size()) + 1) <= limit)
        {
            compact->setProperty(name, object->getProperty(name));
            used += text.length() + name.toString().length() + 4;
        }
        else
        {
            compact->setProperty(name, "[omitted " + juce::String(text.length()) + " characters]");
            omitted.add(name.toString());
        }
    compact->setProperty("_omitted", "Large sections omitted to fit: " + omitted.joinIntoString(", ") +
                                         ". Read them with vibera_inspect (offset/limit) or a narrower tool.");
    return clampToolResult(juce::JSON::toString(juce::var(compact.get()), true), limit);
}

ProducerAgent::~ProducerAgent()
{
    cancel();
    join();
}

void ProducerAgent::join()
{
    if (worker.joinable())
        worker.join();
}

void ProducerAgent::add(AgentEvent::Kind kind, const juce::String &text)
{
    std::lock_guard<std::mutex> lock(mutex);
    log.push_back({kind, text});
}

std::vector<AgentEvent> ProducerAgent::events() const
{
    std::lock_guard<std::mutex> lock(mutex);
    return log;
}

size_t ProducerAgent::eventCount() const
{
    std::lock_guard<std::mutex> lock(mutex);
    return log.size();
}

void ProducerAgent::reset()
{
    if (busy())
        return;
    join();
    std::lock_guard<std::mutex> lock(mutex);
    log.clear();
    conversation = Conversation{};
    conversationKey.clear();
}

juce::Result ProducerAgent::send(const juce::String &userText, const LlmConfig &config, std::vector<ToolSpec> tools,
                                 ToolExecutor executor, int maxSteps, bool mayEditProject)
{
    if (userText.trim().isEmpty())
        return juce::Result::fail("ai.emptyMessage: Type a message first");
    if (busy())
        return juce::Result::fail("ai.busy: The producer is still working; Stop it first");
    if (config.baseUrl.isEmpty() || config.model.isEmpty())
        return juce::Result::fail("ai.notConfigured: Choose a provider and model in Settings > AI");
    join();
    const auto key = juce::String(int(config.dialect)) + "|" + config.baseUrl + "|" + config.model;
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (key != conversationKey)
        {
            conversation = Conversation(config.dialect);
            conversationKey = key;
        }
        conversation.system = producerSystemPrompt(mayEditProject);
        conversation.addUser(userText);
        log.push_back({AgentEvent::Kind::user, userText});
    }
    cancelled.store(false);
    running.store(true);
    worker = std::thread(
        [this, config, tools = std::move(tools), executor = std::move(executor), maxSteps]
        {
            const CancelCheck isCancelled = [this] { return cancelled.load(); };
            for (int step = 0; step < maxSteps; ++step)
            {
                if (isCancelled())
                {
                    add(AgentEvent::Kind::status, "Stopped.");
                    break;
                }
                add(AgentEvent::Kind::status, step == 0 ? "Thinking..." : "Working (step " + juce::String(step + 1) + ")...");
                Conversation snapshot(config.dialect);
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    snapshot = conversation;
                }
                const auto turn = complete(config, snapshot, tools, isCancelled);
                if (!turn.ok())
                {
                    add(isCancelled() ? AgentEvent::Kind::status : AgentEvent::Kind::error,
                        isCancelled() ? juce::String("Stopped.") : turn.error);
                    break;
                }
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    conversation.addAssistant(turn);
                }
                if (turn.text.trim().isNotEmpty())
                    add(AgentEvent::Kind::assistant, turn.text.trim());
                if (turn.refusal || turn.calls.empty())
                    break;
                if (turn.truncated)
                {
                    // Every tool call needs a result before the next user turn; none were run.
                    std::vector<ToolResult> skipped;
                    for (const auto &call : turn.calls)
                        skipped.push_back({call.id, "not run: reply was cut off at max tokens", true});
                    std::lock_guard<std::mutex> lock(mutex);
                    conversation.addToolResults(skipped);
                    log.push_back({AgentEvent::Kind::error,
                                   "The model's reply was cut off (max tokens); tool calls were not run."});
                    break;
                }
                std::vector<ToolResult> results;
                for (const auto &call : turn.calls)
                {
                    add(AgentEvent::Kind::tool, call.name + " " + juce::JSON::toString(call.input, true).substring(0, 400));
                    const auto outcome = isCancelled() ? ToolOutcome{"cancelled by user", true}
                                                       : executor(call.name, call.input, isCancelled);
                    if (outcome.isError)
                        add(AgentEvent::Kind::tool, "  -> error: " + outcome.content.substring(0, 300));
                    results.push_back({call.id, clampToolResult(outcome.content), outcome.isError});
                }
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    conversation.addToolResults(results);
                }
                if (step + 1 == maxSteps)
                    add(AgentEvent::Kind::status, "Reached the step limit; send another message to continue.");
            }
            running.store(false);
        });
    return juce::Result::ok();
}
} // namespace vibera::ai
