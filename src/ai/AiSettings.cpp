// SPDX-License-Identifier: AGPL-3.0-or-later
#include "AiSettings.h"
#if JUCE_WINDOWS
#include <windows.h>
#include <dpapi.h>
#pragma comment(lib, "crypt32.lib")
#endif

namespace vibera::ai
{
const std::vector<ProviderPreset> &providerPresets()
{
    static const std::vector<ProviderPreset> presets{
        {"codex-cli", "ChatGPT subscription (Codex CLI)", Dialect::codexCli, "", "", false, false},
        {"claude-cli", "Claude subscription (Claude Code CLI)", Dialect::claudeCli, "", "", false, false},
        {"anthropic", "Claude API key (Anthropic)", Dialect::anthropic, "https://api.anthropic.com", "claude-opus-5-5"},
        {"openai", "OpenAI API key (billed separately)", Dialect::openai, "https://api.openai.com/v1", ""},
        {"gemini", "Google Gemini", Dialect::openai, "https://generativelanguage.googleapis.com/v1beta/openai",
         ""},
        {"openrouter", "OpenRouter", Dialect::openai, "https://openrouter.ai/api/v1", ""},
        {"deepseek", "DeepSeek", Dialect::openai, "https://api.deepseek.com/v1", ""},
        {"custom", "Custom OpenAI-compatible (cloud)", Dialect::openai, "", ""},
        {"ollama", "Ollama (local)", Dialect::openai, "http://127.0.0.1:11434/v1", "", true, false},
        {"lmstudio", "LM Studio (local)", Dialect::openai, "http://127.0.0.1:1234/v1", "", true, false},
        {"localcustom", "Other local server (OpenAI-compatible)", Dialect::openai, "http://127.0.0.1:8080/v1", "",
         true, false},
    };
    return presets;
}

const juce::StringArray &effortLevels()
{
    static const juce::StringArray levels{"low", "medium", "high", "xhigh", "max", "ultra"};
    return levels;
}

const ProviderPreset *findPreset(const juce::String &id)
{
    for (const auto &preset : providerPresets())
        if (preset.id == id)
            return &preset;
    return nullptr;
}

juce::var AiSettings::toJson() const
{
    auto *root = new juce::DynamicObject;
    root->setProperty("version", 1);
    auto *p = new juce::DynamicObject;
    p->setProperty("preset", provider.preset);
    p->setProperty("baseUrl", provider.baseUrl);
    p->setProperty("model", provider.model);
    p->setProperty("effort", provider.effort);
    root->setProperty("provider", juce::var(p));
    auto *m = new juce::DynamicObject;
    m->setProperty("aceStepUrl", music.aceStepUrl);
    m->setProperty("engineRoot", music.engineRoot.getFullPathName());
    m->setProperty("aceDit", music.aceDit);
    m->setProperty("aceLm", music.aceLm);
    m->setProperty("aceStepInstall", music.aceStepInstall.getFullPathName());
    m->setProperty("aceStepAutoStart", music.aceStepAutoStart);
    m->setProperty("yueInstall", music.yueInstall.getFullPathName());
    m->setProperty("yueLowVram", music.yueLowVram);
    m->setProperty("yueLicenseAccepted", music.yueLicenseAccepted);
    root->setProperty("music", juce::var(m));
    root->setProperty("maxAgentSteps", maxAgentSteps);
    root->setProperty("agentMayEditProject", agentMayEditProject);
    return juce::var(root);
}

AiSettings AiSettings::fromJson(const juce::var &json)
{
    AiSettings s;
    if (!json.isObject())
        return s;
    const auto p = json["provider"];
    if (p.isObject())
    {
        const auto preset = p["preset"].toString();
        if (findPreset(preset))
            s.provider.preset = preset;
        s.provider.baseUrl = p["baseUrl"].toString().trim();
        s.provider.model = p["model"].toString().trim();
        const auto effort = p["effort"].toString();
        if (p.hasProperty("effort") && (effort.isEmpty() || effortLevels().contains(effort)))
            s.provider.effort = effort;
    }
    const auto m = json["music"];
    if (m.isObject())
    {
        if (m["aceStepUrl"].toString().startsWith("http"))
            s.music.aceStepUrl = m["aceStepUrl"].toString();
        s.music.aceDit = m["aceDit"].toString();
        s.music.aceLm = m["aceLm"].toString();
        if (juce::File::isAbsolutePath(m["engineRoot"].toString()))
            s.music.engineRoot = juce::File(m["engineRoot"].toString());
        if (juce::File::isAbsolutePath(m["aceStepInstall"].toString()))
            s.music.aceStepInstall = juce::File(m["aceStepInstall"].toString());
        s.music.aceStepAutoStart = m.getProperty("aceStepAutoStart", true);
        if (juce::File::isAbsolutePath(m["yueInstall"].toString()))
            s.music.yueInstall = juce::File(m["yueInstall"].toString());
        s.music.yueLowVram = m.getProperty("yueLowVram", true);
        s.music.yueLicenseAccepted = m.getProperty("yueLicenseAccepted", false);
    }
    s.maxAgentSteps = juce::jlimit(1, 200, int(json.getProperty("maxAgentSteps", 40)));
    s.agentMayEditProject = json.getProperty("agentMayEditProject", true);
    return s;
}

juce::Result AiSettings::save(const juce::File &file) const
{
    if (!file.getParentDirectory().createDirectory())
        return juce::Result::fail("ai.settingsWrite: Cannot create " + file.getParentDirectory().getFullPathName());
    return file.replaceWithText(juce::JSON::toString(toJson()))
               ? juce::Result::ok()
               : juce::Result::fail("ai.settingsWrite: Cannot write " + file.getFullPathName());
}

AiSettings AiSettings::load(const juce::File &file)
{
    return file.existsAsFile() ? fromJson(juce::JSON::parse(file.loadFileAsString())) : AiSettings{};
}

juce::String AiSettings::effectiveBaseUrl() const
{
    if (provider.baseUrl.isNotEmpty())
        return provider.baseUrl.trimCharactersAtEnd("/");
    const auto *preset = findPreset(provider.preset);
    return preset ? preset->baseUrl : juce::String{};
}

juce::String AiSettings::effectiveModel() const
{
    if (provider.model.isNotEmpty())
        return provider.model;
    const auto *preset = findPreset(provider.preset);
    return preset ? preset->defaultModel : juce::String{};
}

Dialect AiSettings::dialect() const
{
    const auto *preset = findPreset(provider.preset);
    return preset ? preset->dialect : Dialect::openai;
}

juce::File suggestEngineRoot()
{
    constexpr juce::int64 wanted = juce::int64(60) << 30;
#if JUCE_WINDOWS
    const auto local = juce::File::getSpecialLocation(juce::File::windowsLocalAppData).getChildFile("VibeRa Studio/ai");
#else
    const auto local = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                           .getChildFile("VibeRa Studio/ai");
#endif
    if (local.getParentDirectory().getBytesFreeOnVolume() >= wanted)
        return local;
    juce::Array<juce::File> roots;
    juce::File::findFileSystemRoots(roots);
    juce::File best = local;
    juce::int64 most = local.getParentDirectory().getBytesFreeOnVolume();
    for (const auto &root : roots)
        if (root.isOnHardDisk() && !root.isOnRemovableDrive() && root.getBytesFreeOnVolume() > most)
        {
            most = root.getBytesFreeOnVolume();
            best = root.getChildFile("VibeRa AI");
        }
    return best;
}

juce::File managedAiRoot(const MusicEngineConfig &config)
{
    return config.engineRoot != juce::File{} ? config.engineRoot : suggestEngineRoot();
}

bool protectBytes(const juce::MemoryBlock &plain, juce::MemoryBlock &sealed)
{
#if JUCE_WINDOWS
    DATA_BLOB in{DWORD(plain.getSize()), static_cast<BYTE *>(const_cast<void *>(plain.getData()))}, out{};
    if (!CryptProtectData(&in, L"VibeRa Studio AI key", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN,
                          &out))
        return false;
    sealed.replaceAll(out.pbData, out.cbData);
    LocalFree(out.pbData);
    return true;
#else
    juce::ignoreUnused(plain, sealed);
    return false;
#endif
}

bool unprotectBytes(const juce::MemoryBlock &sealed, juce::MemoryBlock &plain)
{
#if JUCE_WINDOWS
    DATA_BLOB in{DWORD(sealed.getSize()), static_cast<BYTE *>(const_cast<void *>(sealed.getData()))}, out{};
    if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out))
        return false;
    plain.replaceAll(out.pbData, out.cbData);
    SecureZeroMemory(out.pbData, out.cbData);
    LocalFree(out.pbData);
    return true;
#else
    juce::ignoreUnused(sealed, plain);
    return false;
#endif
}

namespace
{
juce::var readStore(const juce::File &file)
{
    auto parsed = file.existsAsFile() ? juce::JSON::parse(file.loadFileAsString()) : juce::var{};
    return parsed.isObject() ? parsed : juce::var(new juce::DynamicObject);
}
} // namespace

juce::Result SecretStore::set(const juce::String &id, const juce::String &secret)
{
    auto store = readStore(file);
    if (secret.isEmpty())
        store.getDynamicObject()->removeProperty(id);
    else
    {
        const auto utf8 = secret.toRawUTF8();
        juce::MemoryBlock plain(utf8, std::strlen(utf8)), sealed;
        if (!protectBytes(plain, sealed))
            return juce::Result::fail("ai.keyProtect: Windows could not encrypt the key for this user");
        plain.fillWith(0);
        store.getDynamicObject()->setProperty(id, sealed.toBase64Encoding());
    }
    if (!file.getParentDirectory().createDirectory() || !file.replaceWithText(juce::JSON::toString(store)))
        return juce::Result::fail("ai.keyWrite: Cannot write the encrypted key store");
    return juce::Result::ok();
}

juce::String SecretStore::get(const juce::String &id) const
{
    const auto encoded = readStore(file)[juce::Identifier(id)].toString();
    juce::MemoryBlock sealed, plain;
    if (encoded.isEmpty() || !sealed.fromBase64Encoding(encoded) || !unprotectBytes(sealed, plain))
        return {};
    juce::String secret = juce::String::fromUTF8(static_cast<const char *>(plain.getData()), int(plain.getSize()));
    plain.fillWith(0);
    return secret;
}

bool SecretStore::has(const juce::String &id) const
{
    return readStore(file)[juce::Identifier(id)].toString().isNotEmpty();
}
} // namespace vibera::ai
