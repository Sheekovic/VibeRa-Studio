// SPDX-License-Identifier: AGPL-3.0-or-later
#include "LlmClient.h"

namespace vibera::ai
{
namespace
{
juce::var object()
{
    return juce::var(new juce::DynamicObject);
}
void set(juce::var &o, const char *key, const juce::var &value)
{
    o.getDynamicObject()->setProperty(key, value);
}
bool firstPartyAnthropic(const LlmConfig &config)
{
    return config.baseUrl.startsWithIgnoreCase("https://api.anthropic.com");
}
// Models that accept the server-side refusal fallback ("default" form) on the first-party API.
bool supportsDefaultFallback(const juce::String &model)
{
    return model == "claude-opus-5-5" || model == "claude-opus-5" || model == "claude-fable-5-1" ||
           model == "claude-sonnet-5-5";
}
} // namespace

void Conversation::addUser(const juce::String &text)
{
    auto message = object();
    set(message, "role", "user");
    set(message, "content", text);
    messages.add(message);
}

void Conversation::addAssistant(const LlmTurn &turn)
{
    if (turn.assistantMessage.isObject())
        messages.add(turn.assistantMessage);
}

void Conversation::addToolResults(const std::vector<ToolResult> &results)
{
    if (dialect == Dialect::anthropic)
    {
        // All results of one assistant turn go back in a single user message.
        juce::Array<juce::var> blocks;
        for (const auto &result : results)
        {
            auto block = object();
            set(block, "type", "tool_result");
            set(block, "tool_use_id", result.id);
            set(block, "content", result.content);
            if (result.isError)
                set(block, "is_error", true);
            blocks.add(block);
        }
        auto message = object();
        set(message, "role", "user");
        set(message, "content", blocks);
        messages.add(message);
        return;
    }
    for (const auto &result : results)
    {
        auto message = object();
        set(message, "role", "tool");
        set(message, "tool_call_id", result.id);
        set(message, "content", result.isError ? "ERROR: " + result.content : result.content);
        messages.add(message);
    }
}

juce::String requestUrl(const LlmConfig &config)
{
    const auto base = config.baseUrl.trimCharactersAtEnd("/");
    return config.dialect == Dialect::anthropic ? base + "/v1/messages" : base + "/chat/completions";
}

juce::StringPairArray requestHeaders(const LlmConfig &config)
{
    juce::StringPairArray headers;
    headers.set("Content-Type", "application/json");
    if (config.dialect == Dialect::anthropic)
    {
        headers.set("anthropic-version", "2026-10-21");
        if (config.apiKey.isNotEmpty())
            headers.set("x-api-key", config.apiKey);
        if (firstPartyAnthropic(config) && supportsDefaultFallback(config.model))
            headers.set("anthropic-beta", "server-side-fallback-2026-07-01");
    }
    else if (config.apiKey.isNotEmpty())
        headers.set("Authorization", "Bearer " + config.apiKey);
    return headers;
}

juce::var buildRequest(const LlmConfig &config, const Conversation &conversation, const std::vector<ToolSpec> &tools)
{
    auto request = object();
    set(request, "model", config.model);
    juce::Array<juce::var> toolArray;
    if (config.dialect == Dialect::anthropic)
    {
        set(request, "max_tokens", config.maxTokens);
        if (conversation.system.isNotEmpty())
            set(request, "system", conversation.system);
        for (const auto &tool : tools)
        {
            auto t = object();
            set(t, "name", tool.name);
            set(t, "description", tool.description);
            set(t, "input_schema", tool.inputSchema);
            toolArray.add(t);
        }
        if (!toolArray.isEmpty())
            set(request, "tools", toolArray);
        auto output = object();
        set(output, "effort", config.effort);
        set(request, "output_config", output);
        if (firstPartyAnthropic(config) && supportsDefaultFallback(config.model))
            set(request, "fallbacks", "default");
        set(request, "messages", conversation.messages);
        return request;
    }
    juce::Array<juce::var> messages;
    if (conversation.system.isNotEmpty())
    {
        auto system = object();
        set(system, "role", "system");
        set(system, "content", conversation.system);
        messages.add(system);
    }
    messages.addArray(conversation.messages);
    // No max_tokens: newer OpenAI models reject it (max_completion_tokens), local servers default sensibly.
    set(request, "messages", messages);
    for (const auto &tool : tools)
    {
        auto function = object();
        set(function, "name", tool.name);
        set(function, "description", tool.description);
        set(function, "parameters", tool.inputSchema);
        auto t = object();
        set(t, "type", "function");
        set(t, "function", function);
        toolArray.add(t);
    }
    if (!toolArray.isEmpty())
    {
        set(request, "tools", toolArray);
        set(request, "tool_choice", "auto");
    }
    return request;
}

LlmTurn parseResponse(Dialect dialect, const juce::var &json)
{
    LlmTurn turn;
    if (!json.isObject())
    {
        turn.error = "Model returned a non-JSON response";
        return turn;
    }
    if (dialect == Dialect::anthropic)
    {
        const auto stop = json["stop_reason"].toString();
        turn.refusal = stop == "refusal";
        turn.truncated = stop == "max_tokens";
        if (const auto *content = json["content"].getArray())
            for (const auto &block : *content)
            {
                const auto type = block["type"].toString();
                if (type == "text")
                    turn.text << block["text"].toString();
                else if (type == "tool_use")
                    turn.calls.push_back({block["id"].toString(), block["name"].toString(), block["input"]});
            }
        // Append the assistant turn exactly as returned (thinking/fallback blocks included).
        auto message = object();
        set(message, "role", "assistant");
        set(message, "content", json["content"]);
        turn.assistantMessage = message;
        if (turn.refusal && turn.text.isEmpty())
            turn.text = "The model declined this request.";
        return turn;
    }
    const auto *choices = json["choices"].getArray();
    if (!choices || choices->isEmpty())
    {
        turn.error = "Model response has no choices";
        return turn;
    }
    const auto choice = choices->getFirst();
    const auto message = choice["message"];
    turn.truncated = choice["finish_reason"].toString() == "length";
    turn.refusal = choice["finish_reason"].toString() == "content_filter" || message["refusal"].isString();
    turn.text = message["content"].isString() ? message["content"].toString() : juce::String{};
    if (const auto *calls = message["tool_calls"].getArray())
        for (const auto &call : *calls)
        {
            const auto function = call["function"];
            auto input = juce::JSON::parse(function["arguments"].toString());
            if (!input.isObject())
                input = juce::var(new juce::DynamicObject);
            turn.calls.push_back({call["id"].toString(), function["name"].toString(), input});
        }
    auto stored = object();
    set(stored, "role", "assistant");
    set(stored, "content", turn.text);
    if (message["tool_calls"].isArray())
        set(stored, "tool_calls", message["tool_calls"]);
    turn.assistantMessage = stored;
    return turn;
}

LlmTurn complete(const LlmConfig &config, const Conversation &conversation, const std::vector<ToolSpec> &tools,
                 const CancelCheck &cancelled)
{
    if (config.baseUrl.isEmpty() || config.model.isEmpty())
    {
        LlmTurn turn;
        turn.error = "ai.notConfigured: Choose a provider and model in Settings > AI";
        return turn;
    }
    const auto body = juce::JSON::toString(buildRequest(config, conversation, tools), true);
    const auto response = httpRequest("POST", requestUrl(config), requestHeaders(config), body, config.timeoutMs,
                                      cancelled);
    if (!response.ok())
    {
        LlmTurn turn;
        turn.error = describeHttpFailure(response);
        return turn;
    }
    return parseResponse(config.dialect, juce::JSON::parse(response.body));
}

juce::Result listModels(const LlmConfig &config, juce::StringArray &models, const CancelCheck &cancelled)
{
    models.clear();
    if (config.baseUrl.isEmpty())
        return juce::Result::fail("ai.notConfigured: Base URL required");
    const auto base = config.baseUrl.trimCharactersAtEnd("/");
    const auto url = config.dialect == Dialect::anthropic ? base + "/v1/models?limit=100" : base + "/models";
    auto headers = requestHeaders(config);
    headers.remove("Content-Type");
    headers.remove("anthropic-beta");
    const auto response = httpRequest("GET", url, headers, {}, 30000, cancelled);
    if (!response.ok())
        return juce::Result::fail(describeHttpFailure(response));
    const auto json = juce::JSON::parse(response.body);
    if (const auto *data = json["data"].getArray())
        for (const auto &row : *data)
            if (row["id"].isString())
                models.add(row["id"].toString());
    if (const auto *ollama = json["models"].getArray()) // Native Ollama shape, defensively.
        for (const auto &row : *ollama)
            models.addIfNotAlreadyThere(row["name"].toString());
    models.sortNatural();
    return juce::Result::ok();
}
} // namespace vibera::ai
