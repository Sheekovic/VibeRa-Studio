// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "ai/AiSettings.h"
#include "ai/LlmClient.h"
#include "ai/MusicEngines.h"
#include "ai/ProducerAgent.h"

// Pure AI codecs, settings and key storage. No network or model calls.
static void aiModelTests(const juce::File &folder)
{
    using namespace vibera::ai;
    // Settings round trip and preset defaults.
    AiSettings settings;
    settings.provider = {"ollama", "", "qwen3:8b", "medium"};
    settings.music.aceStepUrl = "http://127.0.0.1:9001";
    settings.music.yueLicenseAccepted = true;
    settings.maxAgentSteps = 12;
    const auto file = folder.getChildFile("ai.json");
    require(settings.save(file).wasOk(), "AI settings save");
    const auto loaded = AiSettings::load(file);
    require(loaded.provider.preset == "ollama" && loaded.provider.model == "qwen3:8b" &&
                loaded.effectiveBaseUrl() == "http://127.0.0.1:11434/v1" && loaded.dialect() == Dialect::openai &&
                loaded.music.aceStepUrl == "http://127.0.0.1:9001" && loaded.music.yueLicenseAccepted &&
                loaded.maxAgentSteps == 12,
            "AI settings round trip with preset defaults");
    AiSettings claude;
    require(claude.effectiveModel() == "claude-opus-5-5" && claude.dialect() == Dialect::anthropic,
            "Default provider is Claude Opus 5.5");
    require(!file.loadFileAsString().containsIgnoreCase("key"), "Settings file never holds API keys");

    // DPAPI-sealed key store.
    const auto secrets = folder.getChildFile("ai-keys.json");
    SecretStore store(secrets);
    const juce::String key = "sk-test-0123456789abcdef";
    require(store.set("openai", key).wasOk() && store.get("openai") == key && store.has("openai"),
            "Encrypted key store round trip");
    require(!secrets.loadFileAsString().contains(key) && !secrets.loadFileAsString().contains("0123456789"),
            "Key file does not contain the plaintext key");
    require(store.set("openai", {}).wasOk() && !store.has("openai") && store.get("openai").isEmpty(),
            "Clearing a key removes it");

    // Anthropic request/response.
    std::vector<ToolSpec> tools{{"vibera_state", "Read state",
                                 juce::JSON::parse(R"({"type":"object","properties":{}})")}};
    LlmConfig anthropic{Dialect::anthropic, "https://api.anthropic.com", "claude-opus-5-5", "k", "high"};
    Conversation chat(Dialect::anthropic);
    chat.system = producerSystemPrompt(true);
    chat.addUser("Make a beat");
    const auto request = buildRequest(anthropic, chat, tools);
    const auto headers = requestHeaders(anthropic);
    require(requestUrl(anthropic) == "https://api.anthropic.com/v1/messages" &&
                headers["anthropic-version"] == "2026-10-21" && headers["x-api-key"] == "k" &&
                headers["anthropic-beta"] == "server-side-fallback-2026-07-01" &&
                request["fallbacks"].toString() == "default" && request["output_config"]["effort"] == "high" &&
                request["tools"][0]["input_schema"].isObject() && !request.hasProperty("temperature") &&
                request["system"].toString().contains("CoProducer"),
            "Anthropic request shape (headers, effort, fallbacks, tools)");
    const auto reply = juce::JSON::parse(R"({"stop_reason":"tool_use","content":[
        {"type":"thinking","thinking":"","signature":"s"},
        {"type":"text","text":"Checking the project."},
        {"type":"tool_use","id":"toolu_1","name":"vibera_state","input":{"x":1}}]})");
    const auto turn = parseResponse(Dialect::anthropic, reply);
    require(turn.ok() && turn.text == "Checking the project." && turn.calls.size() == 1 &&
                turn.calls[0].id == "toolu_1" && int(turn.calls[0].input["x"]) == 1 &&
                turn.assistantMessage["content"].size() == 3,
            "Anthropic tool_use parse keeps the exact assistant content (thinking included)");
    chat.addAssistant(turn);
    chat.addToolResults({{"toolu_1", "{\"ok\":true}", false}});
    require(chat.messages.size() == 3 && chat.messages[2]["content"][0]["type"] == "tool_result" &&
                chat.messages[2]["content"][0]["tool_use_id"] == "toolu_1",
            "Anthropic tool results go back in one user message");
    const auto refusal = parseResponse(Dialect::anthropic, juce::JSON::parse(R"({"stop_reason":"refusal","content":[]})"));
    require(refusal.refusal && refusal.calls.empty(), "Refusal stop reason recognized");
    LlmConfig proxy{Dialect::anthropic, "https://proxy.example/v1", "claude-opus-5-5", "k"};
    require(!buildRequest(proxy, chat, tools).hasProperty("fallbacks"), "Fallbacks only on the first-party API");

    // OpenAI-compatible request/response.
    LlmConfig openai{Dialect::openai, "http://127.0.0.1:11434/v1", "qwen3:8b", ""};
    Conversation oc(Dialect::openai);
    oc.system = "sys";
    oc.addUser("hi");
    const auto oreq = buildRequest(openai, oc, tools);
    require(requestUrl(openai) == "http://127.0.0.1:11434/v1/chat/completions" &&
                oreq["messages"][0]["role"] == "system" && oreq["tools"][0]["type"] == "function" &&
                oreq["tools"][0]["function"]["parameters"].isObject() && !requestHeaders(openai).containsKey("Authorization"),
            "OpenAI-compatible request shape");
    const auto oturn = parseResponse(Dialect::openai, juce::JSON::parse(R"({"choices":[{"finish_reason":"tool_calls",
        "message":{"role":"assistant","content":null,"tool_calls":[{"id":"call_1","type":"function",
        "function":{"name":"vibera_state","arguments":"{\"x\":2}"}}]}}]})"));
    require(oturn.ok() && oturn.calls.size() == 1 && oturn.calls[0].id == "call_1" &&
                int(oturn.calls[0].input["x"]) == 2,
            "OpenAI tool_calls parse");
    oc.addAssistant(oturn);
    oc.addToolResults({{"call_1", "done", false}});
    require(oc.messages.size() == 3 && oc.messages[2]["role"] == "tool" && oc.messages[2]["tool_call_id"] == "call_1",
            "OpenAI tool results use role=tool");

    // Music engine payloads.
    MusicRequest song;
    song.prompt = "dark trap, 808, female vocals";
    song.lyrics = "[Verse]\nhello";
    song.seconds = 90;
    song.bpm = 140;
    song.key = "A minor";
    require(validateMusicRequest(song).wasOk(), "Valid music request");
    const auto ace = aceStepTaskBody(song);
    require(double(ace["audio_duration"]) == 90 && int(ace["bpm"]) == 140 && ace["key_scale"] == "A minor" &&
                ace["use_random_seed"] && ace["audio_format"] == "wav" && ace["lyrics"].toString().contains("[Verse]"),
            "ACE-Step release_task body");
    song.instrumental = true;
    require(aceStepTaskBody(song)["lyrics"] == "[Instrumental]" && yueRequestBody(song)["lyrics"].toString().isEmpty(),
            "Instrumental requests drop lyrics");
    require(yueRequestBody(song)["style"].toString().contains("140 BPM"), "YuE2 style carries tempo and key");
    MusicRequest bad = song;
    bad.seconds = 5;
    require(validateMusicRequest(bad).failed(), "Too-short duration refused");
    bad = song;
    bad.engine = "suno";
    require(validateMusicRequest(bad).failed(), "Unknown engine refused");
    const auto query = juce::JSON::parse(R"({"code":200,"data":[{"task_id":"t1","status":1,
        "result":"[{\"file\":\"/v1/audio?path=%2Ftmp%2Fapi_audio%2Fa.wav\",\"status\":1}]"}]})");
    require(aceStepTaskStatus(query, "t1") == 1 && aceStepAudioReferences(query).size() == 1 &&
                aceStepAudioReferences(query)[0] == "/v1/audio?path=%2Ftmp%2Fapi_audio%2Fa.wav",
            "ACE-Step query_result status and nested audio reference");
    require(clampToolResult(juce::String::repeatedString("x", 30000)).length() < 24200, "Oversized tool results clamp");
    {
        // A large state keeps its top-level values; only big sections are summarised.
        auto state = juce::JSON::parse(R"({"revision":2,"channels":[],"tempo":100})");
        juce::Array<juce::var> notes;
        for (int i = 0; i < 4000; ++i)
            notes.add(juce::JSON::parse(R"({"beat":1.25,"pitch":60,"length":0.5})"));
        state.getDynamicObject()->setProperty("notes", notes);
        const auto text = agentToolText(state, 20000);
        const auto compact = juce::JSON::parse(text);
        require(text.length() <= 20000 && double(compact["tempo"]) == 100. && int(compact["revision"]) == 2 &&
                    compact["channels"].isArray() && compact["notes"].toString().startsWith("[omitted") &&
                    compact["_omitted"].toString().contains("notes"),
                "Compact agent readback keeps scalars and names omitted sections");
        require(agentToolText(juce::JSON::parse(R"({"tempo":90})")) == juce::JSON::toString(juce::JSON::parse(R"({"tempo":90})"), true),
                "Small tool results stay unchanged");
    }

    // ACE-Step model recommendation follows the official VRAM tiers; user choice wins.
    require(recommendAceStep(4 * 1024).lm == "none" && recommendAceStep(4 * 1024).offloadDitToCpu,
            "Under 6 GB: no LM, DiT offloaded");
    require(recommendAceStep(8 * 1024).dit == "acestep-v15-turbo" && recommendAceStep(8 * 1024).lm == "acestep-5Hz-lm-0.6B",
            "8 GB: turbo + 0.6B");
    require(recommendAceStep(18 * 1024).dit == "acestep-v15-xl-turbo" && recommendAceStep(18 * 1024).offloadToCpu,
            "18 GB: XL turbo with CPU offload");
    const auto big = recommendAceStep(32607);
    require(big.dit == "acestep-v15-xl-turbo" && big.lm == "acestep-5Hz-lm-4B" && !big.offloadToCpu,
            "32 GB (RTX 5090): XL turbo + 4B LM, no offload");
    MusicEngineConfig chosen;
    chosen.aceDit = "acestep-v15-xl-sft";
    chosen.aceLm = "none";
    const auto mine = effectiveAceStep(chosen, 16 * 1024);
    require(mine.dit == "acestep-v15-xl-sft" && mine.lm == "none" && mine.offloadToCpu,
            "Manual choice overrides; XL below 20 GB forces CPU offload");
    chosen.aceDit = "bogus";
    require(effectiveAceStep(chosen, 32607).dit == "acestep-v15-xl-turbo", "Unknown model names fall back to the recommendation");
    folder.getChildFile("ai-model-verification.txt").replaceWithText("AI settings/keys/codecs/payloads passed.\n");
}
