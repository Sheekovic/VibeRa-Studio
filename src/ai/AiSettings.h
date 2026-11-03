// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <juce_core/juce_core.h>
#include <vector>

// AI connection settings: language-model providers (cloud with API key or local
// OpenAI-compatible servers) and local music engines (ACE-Step 1.5, YuE2).
// API keys never enter the JSON settings file; they are stored separately,
// encrypted for the current Windows user with DPAPI (SecretStore).
namespace vibera::ai
{
enum class Dialect
{
    anthropic, // POST {base}/v1/messages, x-api-key, anthropic-version
    openai,    // POST {base}/chat/completions, Authorization: Bearer (OpenAI-compatible)
    codexCli,  // Local Codex CLI signed in with ChatGPT: the subscription, no API key
    claudeCli  // Local Claude Code CLI signed in with Claude Pro/Max: the subscription, no API key
};

struct ProviderPreset
{
    juce::String id, label;
    Dialect dialect;
    juce::String baseUrl, defaultModel;
    bool local = false, needsKey = true;
};
// Built-in presets. Models are suggestions; "Fetch models" lists what the account/server offers.
const std::vector<ProviderPreset> &providerPresets();
const ProviderPreset *findPreset(const juce::String &id);

struct ProviderConfig
{
    juce::String preset;  // ProviderPreset id
    juce::String baseUrl; // Empty uses the preset default.
    juce::String model;   // Empty uses the preset default.
    // Anthropic output_config.effort or Codex model_reasoning_effort; ignored by other dialects.
    // Empty means the model's default (Codex); Anthropic then uses "high".
    juce::String effort = "high";
};
// Every effort name VibeRa accepts; each provider/model narrows it further.
const juce::StringArray &effortLevels();

struct MusicEngineConfig
{
    juce::String aceStepUrl = "http://127.0.0.1:8001";
    juce::File engineRoot; // Where engines install; empty: chosen automatically on first install.
    // ACE-Step 1.5 models; empty = automatic recommendation from the detected GPU.
    juce::String aceDit; // e.g. acestep-v15-turbo, acestep-v15-xl-turbo
    juce::String aceLm;  // acestep-5Hz-lm-0.6B / -1.7B / -4B, or "none"
    juce::File aceStepInstall; // Empty uses the managed install folder.
    bool aceStepAutoStart = true;
    juce::File yueInstall; // Empty uses the managed install folder.
    bool yueLowVram = true;
    bool yueLicenseAccepted = false; // CC BY-NC 4.0 weights notice acknowledged once.
};

struct AiSettings
{
    ProviderConfig provider{"anthropic"};
    MusicEngineConfig music;
    int maxAgentSteps = 40;
    bool agentMayEditProject = true;

    juce::var toJson() const;
    static AiSettings fromJson(const juce::var &);
    juce::Result save(const juce::File &) const;
    static AiSettings load(const juce::File &);
    // Effective values after preset defaults.
    juce::String effectiveBaseUrl() const;
    juce::String effectiveModel() const;
    Dialect dialect() const;
};

// Root for managed engine installs: the configured engineRoot, else
// %LOCALAPPDATA%/VibeRa Studio/ai when that drive has >= 60 GB free, else
// "VibeRa AI" on the fixed (non-removable) local drive with the most free space.
juce::File managedAiRoot(const MusicEngineConfig & = {});
juce::File suggestEngineRoot();

// Per-user encrypted API keys (DPAPI on Windows). Keys are addressed by provider preset id.
class SecretStore
{
  public:
    explicit SecretStore(juce::File file) : file(std::move(file)) {}
    juce::Result set(const juce::String &id, const juce::String &secret); // Empty removes.
    juce::String get(const juce::String &id) const;
    bool has(const juce::String &id) const;

  private:
    juce::File file;
};

// Exposed for tests: DPAPI protect/unprotect round trip of arbitrary bytes.
bool protectBytes(const juce::MemoryBlock &plain, juce::MemoryBlock &sealed);
bool unprotectBytes(const juce::MemoryBlock &sealed, juce::MemoryBlock &plain);
} // namespace vibera::ai
