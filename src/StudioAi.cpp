// SPDX-License-Identifier: AGPL-3.0-or-later
// AI Producer integration: settings/keys, the agent's tool executor (canonical
// MCP operations on the message thread), local music generation and placement.
#include "Studio.h"
#include "McpSchema.h"
#include "CoProducerDock.h"
#include "AiPage.h"
#include "NativeWindowStyle.h"

namespace vibera
{
namespace
{
juce::var obj()
{
    return juce::var(new juce::DynamicObject);
}
void set(juce::var &o, const char *k, const juce::var &v)
{
    o.getDynamicObject()->setProperty(k, v);
}
// Runs fn on the message thread and waits, honouring cancellation (the posted
// call may still run later; it only touches shared state and a SafePointer).
bool runOnMessageThread(std::function<void()> fn, const ai::CancelCheck &cancelled)
{
    if (juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        fn();
        return true;
    }
    auto done = std::make_shared<juce::WaitableEvent>();
    juce::MessageManager::callAsync([fn = std::move(fn), done] { fn(); done->signal(); });
    while (!done->wait(50))
        if (cancelled && cancelled())
            return false;
    return true;
}
const juce::StringArray readOnlyTools{"vibera_state", "vibera_inspect", "vibera_commands", "vibera_snapshot", "vibera_update",
                                      "vibera_selection"};
// Diagnostic pointer/key simulation and this tool itself are never offered to the agent.
const juce::StringArray hiddenTools{"vibera_ai", "vibera_audioRecording", "vibera_pointer", "vibera_key"};
} // namespace

juce::File Studio::aiSettingsFile() const
{
    if (workflowTestSettings)
        return workflowTestSettings->getFile().getSiblingFile(workflowTestSettings->getFile().getFileNameWithoutExtension() +
                                                              "-ai.json");
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("VibeRa Studio/ai.json");
}

ai::SecretStore Studio::aiSecrets() const
{
    return ai::SecretStore(aiSettingsFile().getSiblingFile(aiSettingsFile().getFileNameWithoutExtension() + "-keys.dat"));
}

void Studio::saveAiSettings()
{
    if (auto saved = aiSettings.save(aiSettingsFile()); saved.failed())
        status = saved.getErrorMessage();
}

ai::LlmConfig Studio::aiLlmConfig() const
{
    ai::LlmConfig config;
    config.dialect = aiSettings.dialect();
    config.baseUrl = aiSettings.effectiveBaseUrl();
    config.model = aiSettings.effectiveModel();
    // Anthropic accepts low..max; an empty (model default) or Codex-only level falls back to high.
    const auto effort = aiSettings.provider.effort;
    config.effort = juce::StringArray{"low", "medium", "high", "xhigh", "max"}.contains(effort) ? effort : "high";
    config.apiKey = aiSecrets().get(aiSettings.provider.preset);
    return config;
}

juce::var Studio::aiToolDefinition()
{
    return juce::JSON::parse(R"JSON({"name":"vibera_ai","description":"CoProducer (AI chat producer) and local AI music. state: provider/engine/job/chat status (never API keys), state.panel (docked CoProducer side panel) state.codex (local Codex CLI install/ChatGPT sign-in, models[] with slug/name/efforts/defaultEffort) and state.claudeCli (local Claude Code CLI install/sign-in/subscription, models aliases). Presets codex-cli and claude-cli use the user's ChatGPT or Claude subscription through the signed-in CLI instead of an API key; detectAgents re-checks both in the background (state.agent.detecting). configure: provider {preset, baseUrl, model, effort: \"\" (model default)|low|medium|high|xhigh|max|ultra; Codex sends it only when the model supports it; Claude Code accepts low..max}, write-only apiKey, music {aceStepUrl, aceStepAutoStart, aceDit, aceLm (ACE-Step models, \"\" automatic), engineRoot (absolute folder), yueLowVram, yueLicenseAccepted}, maxAgentSteps, agentMayEditProject. testConnection then state.connection: lists models. generateMusic: engine ace-step|yue2, prompt, lyrics, language, seconds 10..600, bpm 0|30..300, key, seed, instrumental, beat, lane; installs/starts the engine automatically on first use (large downloads), then places the audio as a Playlist clip (own Undo step); poll state.music. prepareEngine: install/check an engine without generating. cancelMusic, stopServer. chat: message to the in-app producer agent (it edits the project with the same canonical tools); events: since index; cancelChat; resetChat. panel: show/hide the docked CoProducer panel {visible, tab chat|midi}.","inputSchema":{"type":"object","additionalProperties":false,"properties":{
      "action":{"type":"string","enum":["state","configure","testConnection","generateMusic","prepareEngine","cancelMusic","stopServer","chat","events","cancelChat","resetChat","detectAgents","panel"]},
      "provider":{"type":"object"},"apiKey":{"type":"string","maxLength":4096},"music":{"type":"object"},
      "maxAgentSteps":{"type":"integer","minimum":1,"maximum":200},"agentMayEditProject":{"type":"boolean"},
      "engine":{"type":"string","enum":["ace-step","yue2"]},"prompt":{"type":"string","maxLength":2000},
      "lyrics":{"type":"string","maxLength":10000},"language":{"type":"string","maxLength":16},
      "seconds":{"type":"number","minimum":10,"maximum":600},"bpm":{"type":"number","minimum":0,"maximum":300},
      "key":{"type":"string","maxLength":32},"seed":{"type":"integer"},"instrumental":{"type":"boolean"},
      "beat":{"type":"number","minimum":0},"lane":{"type":"integer","minimum":-1,"maximum":127},
      "message":{"type":"string","maxLength":20000},"since":{"type":"integer","minimum":0},
      "visible":{"type":"boolean"},"tab":{"type":"string","enum":["chat","midi"]}},"required":["action"]}})JSON");
}

std::vector<ai::ToolSpec> Studio::aiTools() const
{
    std::vector<ai::ToolSpec> tools;
    const auto definitions = mcpTools(); // Keep the array alive while iterating.
    if (const auto *all = definitions.getArray())
        for (const auto &tool : *all)
        {
            const auto name = tool["name"].toString();
            if (hiddenTools.contains(name))
                continue;
            if (!aiSettings.agentMayEditProject && !readOnlyTools.contains(name))
                continue;
            tools.push_back({name, tool["description"].toString(), tool["inputSchema"]});
        }
    if (aiSettings.agentMayEditProject)
        tools.push_back(
            {"vibera_generate_music",
             "Generate a full audio track with a local model and place it in the Playlist as an audio clip. "
             "engine: ace-step (fast, 10..600 s, vocals or instrumental) or yue2 (lyrics-to-song, slower, needs a "
             "big GPU). prompt: genre, mood, instruments, vocal type. lyrics with [Verse]/[Chorus] tags, or "
             "instrumental=true. bpm 0 lets the model choose; use the project tempo to stay in sync. beat/lane "
             "place the clip (lane -1 = first free lane). The first use installs the engine automatically and can "
             "take a long time.",
             juce::JSON::parse(R"({"type":"object","properties":{
                "engine":{"type":"string","enum":["ace-step","yue2"]},"prompt":{"type":"string"},
                "lyrics":{"type":"string"},"language":{"type":"string"},"seconds":{"type":"number"},
                "bpm":{"type":"number"},"key":{"type":"string"},"seed":{"type":"integer"},
                "instrumental":{"type":"boolean"},"beat":{"type":"number"},"lane":{"type":"integer"}},
                "required":["prompt"]})")});
    return tools;
}

juce::File Studio::aiOutputFolder() const
{
    return projectFile != juce::File{}
               ? projectFile.getParentDirectory().getChildFile("AI Music")
               : juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("VibeRa Studio/AI Music");
}

void Studio::pinAiEngineRoot()
{
    if (aiSettings.music.engineRoot == juce::File{})
    {
        aiSettings.music.engineRoot = ai::suggestEngineRoot();
        saveAiSettings();
    }
}

juce::Result Studio::aiStartMusic(const ai::MusicRequest &request, double beat, int lane)
{
    pinAiEngineRoot();
    if (!std::isfinite(beat) || beat < 0 || lane < -1 || lane > 127)
        return juce::Result::fail("music.invalidPlacement: beat >= 0 and lane -1..127");
    if (auto started = aiMusic.start(request, aiSettings.music, aiOutputFolder()); started.failed())
        return started;
    aiPlacement = {controller.projectIdentity, beat, lane, true, {}};
    return juce::Result::ok();
}

void Studio::pollAiMusic()
{
    if (!aiPlacement.pending)
        return;
    const auto job = aiMusic.snapshot();
    if (job.state == ai::JobState::running)
        return;
    aiPlacement.pending = false;
    if (job.state != ai::JobState::succeeded || !job.audio.existsAsFile())
    {
        status = job.state == ai::JobState::cancelled ? "AI music cancelled" : "AI music failed: " + job.message;
        return;
    }
    if (aiPlacement.project != controller.projectIdentity)
    {
        status = "AI music saved to " + job.audio.getFullPathName() + " (project changed, not placed)";
        return;
    }
    int lane = aiPlacement.lane;
    if (lane < 0)
    {
        lane = 0;
        for (const auto &clip : controller.project.placements)
            lane = std::max(lane, clip.lane + 1);
        for (const auto &clip : controller.project.audioPlacements)
            lane = std::max(lane, clip.lane + 1);
        lane = std::min(lane, 127);
    }
    aiPlacement.placed = job.audio;
    importAudioClip(job.audio, aiPlacement.beat, lane);
}

ai::ToolOutcome Studio::aiExecuteTool(const juce::String &name, const juce::var &input,
                                      const ai::CancelCheck &cancelled, bool mayEdit)
{
    if (hiddenTools.contains(name))
        return {"Tool not available to the producer: " + name, true};
    if (!mayEdit && !readOnlyTools.contains(name))
        return {"Read-only mode: the user disabled project edits for the AI Producer", true};
    auto safe = juce::Component::SafePointer<Studio>(this);
    if (name == "vibera_generate_music")
    {
        auto request = ai::musicRequestFromJson(input);
        if (!input.hasProperty("engine"))
            request.engine = "ace-step";
        const double beat = double(input.getProperty("beat", 0));
        const int lane = int(input.getProperty("lane", -1));
        auto started = std::make_shared<juce::Result>(juce::Result::ok());
        if (!runOnMessageThread([safe, request, beat, lane, started]
                                { *started = safe ? safe->aiStartMusic(request, beat, lane)
                                                  : juce::Result::fail("Studio closed"); },
                                cancelled))
            return {"cancelled", true};
        if (started->failed())
            return {started->getErrorMessage(), true};
        // Wait for the job on this agent thread (installs can take a long time).
        while (aiMusic.busy())
        {
            if (cancelled && cancelled())
            {
                aiMusic.cancel();
                return {"cancelled by user", true};
            }
            juce::Thread::sleep(250);
        }
        const auto job = aiMusic.snapshot();
        if (job.state != ai::JobState::succeeded)
            return {"Music generation failed: " + job.message, true};
        auto placed = std::make_shared<juce::var>();
        runOnMessageThread([safe, placed]
                           {
                               if (!safe)
                                   return;
                               safe->pollAiMusic(); // Places immediately instead of waiting for the timer.
                               auto o = obj();
                               set(o, "file", safe->aiPlacement.placed.getFullPathName());
                               set(o, "placement", "Importing into the Playlist as a new Audio Clip Channel; read "
                                                   "vibera_inspect clips to get its id once imported.");
                               *placed = o;
                           },
                           cancelled);
        return {juce::JSON::toString(*placed, true), false};
    }
    auto outcome = std::make_shared<ai::ToolOutcome>();
    if (!runOnMessageThread(
            [safe, name, input, outcome]
            {
                if (!safe)
                {
                    *outcome = {"Studio closed", true};
                    return;
                }
                try
                {
                    const auto result = safe->mcpCall(name, input.isObject() ? input : obj());
                    const auto data = result.hasProperty("structuredContent") ? result["structuredContent"] : result;
                    *outcome = {ai::agentToolText(data), bool(result.getProperty("isError", false))};
                }
                catch (const McpFailure &failure)
                {
                    *outcome = {failure.code + ": " + failure.what(), true};
                }
                catch (const std::exception &error)
                {
                    *outcome = {error.what(), true};
                }
            },
            cancelled))
        return {"cancelled by user", true};
    return *outcome;
}

juce::Result Studio::verifyAiUi(const juce::File &folder)
{
    folder.createDirectory();
    auto capture = [&](juce::Component &component, const juce::String &name)
    {
        component.setLookAndFeel(&getLookAndFeel());
        juce::Image image(juce::Image::ARGB, component.getWidth(), component.getHeight(), true);
        {
            juce::Graphics g(image);
            g.fillAll(panel); // The dialog background the page is shown on.
            component.paintEntireComponent(g, true);
        }
        auto file = folder.getChildFile(name);
        file.deleteFile();
        juce::FileOutputStream stream(file);
        return stream.openedOk() && juce::PNGImageFormat{}.writeImageToStream(image, stream);
    };
    AiPage page(*this);
    page.setSize(660, 900);
    CoProducerDock dock(*this);
    dock.setSize(420, 760);
    if (!capture(page, "ai-settings.png") || !capture(dock, "coproducer-chat.png"))
        return juce::Result::fail("AI UI capture failed");
    dock.setTab(1);
    if (!capture(dock, "coproducer-midi.png"))
        return juce::Result::fail("AI UI capture failed");
    const auto saved = aiSettings.provider;
    aiSettings.provider = {"codex-cli"}; // Subscription provider layout (not persisted).
    AiPage codexPage(*this);
    codexPage.setSize(660, 900);
    const bool codexCaptured = capture(codexPage, "ai-settings-codex.png");
    aiSettings.provider = saved;
    if (!codexCaptured)
        return juce::Result::fail("AI UI capture failed");
    return juce::Result::ok();
}

void Studio::showCoProducer(int tab)
{
    if (!coProducerDock)
    {
        coProducerDock = std::make_unique<CoProducerDock>(*this);
        coProducerDock->setLookAndFeel(&getLookAndFeel());
        addChildComponent(*coProducerDock);
    }
    if (tab >= 0)
        coProducerTab = tab;
    if (coProducerDock->currentTab() != coProducerTab)
        coProducerDock->setTab(coProducerTab);
    coProducerShown = true;
    coProducerDock->setVisible(true);
    resized();
    repaint();
}

void Studio::hideCoProducer()
{
    coProducerShown = false;
    if (coProducerDock)
    {
        coProducerTab = coProducerDock->currentTab();
        coProducerDock->setVisible(false); // Kept alive: the conversation and MIDI candidates survive.
    }
    resized();
    repaint();
}

void Studio::aiDetectAgents()
{
    if (aiDetecting.exchange(true))
        return;
    if (aiDetect.thread.joinable())
        aiDetect.thread.join(); // The previous pass has finished (aiDetecting was false).
    auto safe = juce::Component::SafePointer<Studio>(this);
    aiDetect.thread = std::thread(
        [safe, detecting = &aiDetecting]
        {
            const auto codex = ai::cachedCodex(true);
            const auto claude = ai::cachedClaudeCli(true);
            detecting->store(false); // Studio joins this thread before destroying the flag.
            juce::MessageManager::callAsync(
                [safe, codex, claude]
                {
                    if (!safe || (!codex.loggedIn && !claude.loggedIn))
                        return;
                    // Prefer the user's signed-in subscription over a key-based provider with no key.
                    const auto *preset = ai::findPreset(safe->aiSettings.provider.preset);
                    if (preset && preset->needsKey && !safe->aiSecrets().has(preset->id))
                    {
                        safe->aiSettings.provider = {codex.loggedIn ? "codex-cli" : "claude-cli"};
                        safe->saveAiSettings();
                    }
                });
        });
}

juce::Result Studio::aiSend(const juce::String &text)
{
    if (aiUsesCodex())
    {
        if (aiBusy())
            return juce::Result::fail("ai.busy: The producer is still working; Stop it first");
        const bool mayEditCodex = aiSettings.agentMayEditProject;
        auto safeCodex = juce::Component::SafePointer<Studio>(this);
        if (!aiBridge.start(aiTools(),
                            [safeCodex, mayEditCodex](const juce::String &name, const juce::var &input,
                                                      const ai::CancelCheck &cancelled) -> ai::ToolOutcome
                            {
                                if (!safeCodex)
                                    return {"Studio closed", true};
                                return safeCodex->aiExecuteTool(name, input, cancelled, mayEditCodex);
                            }))
            return juce::Result::fail("ai.bridge: Could not open the local tool bridge");
        if (aiCliPreset != aiSettings.provider.preset)
        {
            aiCli.reset(); // A Codex thread cannot be resumed by Claude, nor the reverse.
            aiCliPreset = aiSettings.provider.preset;
        }
        const auto executable = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
        const auto work = aiSettingsFile().getSiblingFile(
            aiSettings.dialect() == ai::Dialect::claudeCli ? "claude-work" : "codex-work");
        if (aiSettings.dialect() == ai::Dialect::claudeCli)
        {
            const auto claude = ai::cachedClaudeCli();
            if (!claude.installed)
                return juce::Result::fail(
                    "ai.claudeMissing: Claude Code CLI not found (npm i -g @anthropic-ai/claude-code), then sign in");
            if (!claude.loggedIn)
                return juce::Result::fail("ai.claudeLogin: Run `claude auth login` and sign in first");
            ai::ClaudeRun run;
            run.prompt = text;
            run.model = aiSettings.provider.model;
            run.effort = aiSettings.provider.effort;
            run.bridgeExecutable = executable;
            run.workFolder = work;
            run.bridgePort = aiBridge.port();
            run.bridgeToken = aiBridge.token();
            return aiCli.send(text, ai::claudeTurn(claude, run, mayEditCodex));
        }
        const auto codex = ai::cachedCodex();
        if (!codex.installed)
            return juce::Result::fail("ai.codexMissing: Codex CLI not found (npm i -g @openai/codex), then sign in");
        if (!codex.loggedIn)
            return juce::Result::fail("ai.codexLogin: Run `codex login` and sign in with ChatGPT first");
        ai::CodexRun run;
        run.prompt = text;
        run.model = aiSettings.provider.model;
        run.effort = ai::codexEffort(codex, run.model, aiSettings.provider.effort);
        run.bridgeExecutable = executable;
        run.workFolder = work;
        run.bridgePort = aiBridge.port();
        run.bridgeToken = aiBridge.token();
        return aiCli.send(text, ai::codexTurn(codex, run, mayEditCodex));
    }
    const auto config = aiLlmConfig();
    const auto *preset = ai::findPreset(aiSettings.provider.preset);
    if (preset && preset->needsKey && config.apiKey.isEmpty())
        return juce::Result::fail("ai.noKey: Add your API key in Settings > AI");
    const bool mayEdit = aiSettings.agentMayEditProject;
    auto safe = juce::Component::SafePointer<Studio>(this);
    return aiAgent.send(text, config, aiTools(),
                        [safe, mayEdit](const juce::String &name, const juce::var &input,
                                        const ai::CancelCheck &cancelled) -> ai::ToolOutcome
                        {
                            if (!safe)
                                return {"Studio closed", true};
                            return safe->aiExecuteTool(name, input, cancelled, mayEdit);
                        },
                        aiSettings.maxAgentSteps, mayEdit);
}

void Studio::aiTestConnection()
{
    if (aiConnection.running.load())
        return;
    if (aiUsesCodex())
    {
        if (aiConnection.worker.joinable())
            aiConnection.worker.join();
        aiConnection.running.store(true);
        aiConnection.worker = std::thread(
            [this, claudeCli = aiSettings.dialect() == ai::Dialect::claudeCli]
            {
                if (claudeCli)
                {
                    const auto claude = ai::cachedClaudeCli(true);
                    std::lock_guard<std::mutex> lock(aiConnection.mutex);
                    aiConnection.ok = claude.loggedIn;
                    aiConnection.models.clear();
                    for (const auto &model : ai::claudeCliModels())
                        if (model.isNotEmpty())
                            aiConnection.models.add(model);
                    aiConnection.message =
                        !claude.installed  ? juce::String("Claude Code CLI not found: npm i -g @anthropic-ai/claude-code")
                        : !claude.loggedIn ? "Claude Code found but not signed in: run `claude auth login`"
                                           : "Ready: Claude Code " + claude.version.upToFirstOccurrenceOf(" ", false, false) +
                                                 " - signed in (" +
                                                 (claude.subscription.isNotEmpty() ? claude.subscription + " plan"
                                                                                   : claude.authMethod) +
                                                 ")";
                    aiConnection.running.store(false);
                    return;
                }
                const auto codex = ai::cachedCodex(true);
                std::lock_guard<std::mutex> lock(aiConnection.mutex);
                aiConnection.ok = codex.loggedIn;
                aiConnection.models.clear();
                for (const auto &model : codex.models)
                    aiConnection.models.add(model.slug);
                aiConnection.message = !codex.installed ? juce::String("Codex CLI not found: npm i -g @openai/codex")
                                       : !codex.loggedIn ? "Codex found but not signed in: run `codex login`"
                                                         : "Ready: " + codex.version + " - " + codex.login;
                aiConnection.running.store(false);
            });
        return;
    }
    if (aiConnection.worker.joinable())
        aiConnection.worker.join();
    aiConnection.running.store(true);
    {
        std::lock_guard<std::mutex> lock(aiConnection.mutex);
        aiConnection.message = "Testing...";
        aiConnection.ok = false;
        aiConnection.models.clear();
    }
    const auto config = aiLlmConfig();
    aiConnection.worker = std::thread(
        [this, config]
        {
            juce::StringArray models;
            const auto result = ai::listModels(config, models);
            std::lock_guard<std::mutex> lock(aiConnection.mutex);
            aiConnection.ok = result.wasOk();
            aiConnection.models = models;
            aiConnection.message = result.wasOk() ? "Connected: " + juce::String(models.size()) + " models available"
                                                  : result.getErrorMessage();
            aiConnection.running.store(false);
        });
}

juce::var Studio::mcpAiOperation(const juce::var &args)
{
    const auto action = args["action"].toString();
    auto state = [&]
    {
        auto o = obj();
        auto provider = obj();
        set(provider, "preset", aiSettings.provider.preset);
        set(provider, "baseUrl", aiSettings.effectiveBaseUrl());
        set(provider, "model", aiSettings.effectiveModel());
        set(provider, "effort", aiSettings.provider.effort);
        set(provider, "hasApiKey", aiSecrets().has(aiSettings.provider.preset));
        set(o, "provider", provider);
        juce::Array<juce::var> presets;
        for (const auto &preset : ai::providerPresets())
        {
            auto p = obj();
            set(p, "id", preset.id);
            set(p, "label", preset.label);
            set(p, "dialect", preset.dialect == ai::Dialect::anthropic ? "anthropic" : "openai");
            set(p, "local", preset.local);
            set(p, "needsKey", preset.needsKey);
            presets.add(p);
        }
        set(o, "presets", presets);
        {
            std::lock_guard<std::mutex> lock(aiConnection.mutex);
            auto c = obj();
            set(c, "running", aiConnection.running.load());
            set(c, "ok", aiConnection.ok);
            set(c, "message", aiConnection.message);
            juce::Array<juce::var> models;
            for (const auto &m : aiConnection.models)
                models.add(m);
            set(c, "models", models);
            set(o, "connection", c);
        }
        const auto job = aiMusic.snapshot();
        auto music = obj();
        set(music, "aceStepUrl", aiSettings.music.aceStepUrl);
        set(music, "engineRoot", ai::managedAiRoot(aiSettings.music).getFullPathName());
        {
            const auto &gpu = ai::cachedGpu();
            auto g = obj();
            set(g, "nvidia", gpu.nvidia);
            set(g, "name", gpu.name);
            set(g, "vramMb", gpu.vramMb);
            set(music, "gpu", g);
            const auto rec = ai::recommendAceStep(gpu.vramMb);
            const auto use = ai::effectiveAceStep(aiSettings.music, gpu.vramMb);
            auto r = obj();
            set(r, "dit", rec.dit);
            set(r, "lm", rec.lm);
            set(r, "reason", rec.reason);
            set(music, "aceRecommended", r);
            auto e = obj();
            set(e, "dit", use.dit);
            set(e, "lm", use.lm);
            set(e, "offloadToCpu", use.offloadToCpu);
            set(music, "aceEffective", e);
            set(music, "aceDit", aiSettings.music.aceDit);
            set(music, "aceLm", aiSettings.music.aceLm);
        }
        set(music, "aceStepInstalled", ai::MusicEngines::aceStepInstalled(aiSettings.music));
        set(music, "aceStepServerOwned", aiMusic.serverRunning());
        set(music, "yueInstalled", ai::MusicEngines::yueInstalled(aiSettings.music));
        set(music, "yueLicenseAccepted", aiSettings.music.yueLicenseAccepted);
        set(music, "jobState", juce::StringArray{"idle", "running", "succeeded", "failed", "cancelled"}[int(job.state)]);
        set(music, "engine", job.engine);
        set(music, "stage", job.stage);
        set(music, "progress", job.progress);
        set(music, "message", job.message);
        set(music, "audio", job.audio.getFullPathName());
        set(music, "placementPending", aiPlacement.pending);
        set(o, "music", music);
        auto agent = obj();
        set(agent, "busy", aiBusy());
        set(agent, "events", juce::int64(aiEvents().size()));
        set(agent, "detecting", aiDetecting.load());
        {
            const auto codex = ai::knownCodex().value_or(ai::CodexStatus{});
            auto c = obj();
            set(c, "installed", codex.installed);
            set(c, "loggedIn", codex.loggedIn);
            set(c, "chatgpt", codex.chatgpt);
            set(c, "version", codex.version);
            juce::Array<juce::var> models;
            for (const auto &model : codex.models)
            {
                auto m = obj();
                set(m, "slug", model.slug);
                set(m, "name", model.name);
                set(m, "defaultEffort", model.defaultEffort);
                juce::Array<juce::var> efforts;
                for (const auto &level : model.efforts)
                    efforts.add(level);
                set(m, "efforts", juce::var(efforts));
                models.add(m);
            }
            set(c, "models", juce::var(models));
            set(c, "effectiveEffort",
                ai::codexEffort(codex, aiSettings.provider.model, aiSettings.provider.effort));
            set(o, "codex", c);
            const auto claude = ai::knownClaudeCli().value_or(ai::ClaudeCliStatus{});
            auto k = obj();
            set(k, "installed", claude.installed);
            set(k, "loggedIn", claude.loggedIn);
            set(k, "subscription", claude.subscription);
            set(k, "authMethod", claude.authMethod);
            set(k, "version", claude.version);
            juce::Array<juce::var> aliases;
            for (const auto &model : ai::claudeCliModels())
                if (model.isNotEmpty())
                    aliases.add(model);
            set(k, "models", juce::var(aliases));
            set(k, "effectiveEffort", ai::claudeCliEffort(aiSettings.provider.effort));
            set(o, "claudeCli", k);
        }
        set(agent, "maxSteps", aiSettings.maxAgentSteps);
        set(agent, "mayEditProject", aiSettings.agentMayEditProject);
        set(o, "agent", agent);
        auto dock = obj();
        set(dock, "visible", coProducerShown);
        set(dock, "tab", (coProducerDock ? coProducerDock->currentTab() : coProducerTab) == 1 ? "midi" : "chat");
        set(dock, "width", int(coProducerWidth));
        set(o, "panel", dock);
        return o;
    };
    if (action == "state")
        return state();
    if (action == "configure")
    {
        const auto provider = args["provider"];
        if (provider.isObject())
        {
            if (provider.hasProperty("preset"))
            {
                if (!ai::findPreset(provider["preset"].toString()))
                    throw McpFailure("invalid_arguments", "Unknown provider preset");
                aiSettings.provider.preset = provider["preset"].toString();
            }
            if (provider.hasProperty("baseUrl"))
                aiSettings.provider.baseUrl = provider["baseUrl"].toString().trim();
            if (provider.hasProperty("model"))
                aiSettings.provider.model = provider["model"].toString().trim();
            if (provider.hasProperty("effort"))
            {
                const auto effort = provider["effort"].toString();
                if (effort.isNotEmpty() && !ai::effortLevels().contains(effort))
                    throw McpFailure("invalid_arguments", "effort must be empty (model default) or " +
                                                              ai::effortLevels().joinIntoString("|"));
                aiSettings.provider.effort = provider["effort"].toString();
            }
        }
        const auto music = args["music"];
        if (music.isObject())
        {
            if (music.hasProperty("engineRoot"))
            {
                const auto root = music["engineRoot"].toString();
                if (root.isNotEmpty() && !juce::File::isAbsolutePath(root))
                    throw McpFailure("invalid_arguments", "engineRoot must be an absolute folder");
                aiSettings.music.engineRoot = root.isEmpty() ? juce::File{} : juce::File(root);
            }
            if (music.hasProperty("aceDit"))
            {
                const auto dit = music["aceDit"].toString();
                if (dit.isNotEmpty() && !ai::aceStepDitModels().contains(dit))
                    throw McpFailure("invalid_arguments", "aceDit must be empty (auto) or one of " +
                                                              ai::aceStepDitModels().joinIntoString(", "));
                aiSettings.music.aceDit = dit;
            }
            if (music.hasProperty("aceLm"))
            {
                const auto lm = music["aceLm"].toString();
                if (lm.isNotEmpty() && !ai::aceStepLmModels().contains(lm))
                    throw McpFailure("invalid_arguments", "aceLm must be empty (auto) or one of " +
                                                              ai::aceStepLmModels().joinIntoString(", "));
                aiSettings.music.aceLm = lm;
            }
            if (music.hasProperty("aceStepUrl"))
                aiSettings.music.aceStepUrl = music["aceStepUrl"].toString();
            if (music.hasProperty("aceStepAutoStart"))
                aiSettings.music.aceStepAutoStart = bool(music["aceStepAutoStart"]);
            if (music.hasProperty("yueLowVram"))
                aiSettings.music.yueLowVram = bool(music["yueLowVram"]);
            if (music.hasProperty("yueLicenseAccepted"))
                aiSettings.music.yueLicenseAccepted = bool(music["yueLicenseAccepted"]);
        }
        if (args.hasProperty("maxAgentSteps"))
            aiSettings.maxAgentSteps = juce::jlimit(1, 200, int(args["maxAgentSteps"]));
        if (args.hasProperty("agentMayEditProject"))
            aiSettings.agentMayEditProject = bool(args["agentMayEditProject"]);
        if (args.hasProperty("apiKey"))
            if (auto stored = aiSecrets().set(aiSettings.provider.preset, args["apiKey"].toString()); stored.failed())
                throw McpFailure("invalid_state", stored.getErrorMessage());
        saveAiSettings();
        return state();
    }
    if (action == "testConnection")
    {
        aiTestConnection();
        auto o = state();
        set(o, "completionState", "pending");
        return o;
    }
    if (action == "generateMusic" || action == "prepareEngine")
    {
        pinAiEngineRoot();
        if (action == "prepareEngine")
        {
            if (auto ok = aiMusic.prepare(args.getProperty("engine", "ace-step").toString(), aiSettings.music); ok.failed())
                throw McpFailure("invalid_state", ok.getErrorMessage());
        }
        else
        {
            auto request = ai::musicRequestFromJson(args);
            if (auto ok = aiStartMusic(request, double(args.getProperty("beat", 0)), int(args.getProperty("lane", -1)));
                ok.failed())
                throw McpFailure(ok.getErrorMessage().upToFirstOccurrenceOf(":", false, false), ok.getErrorMessage());
        }
        auto o = state();
        set(o, "completionState", "pending");
        return o;
    }
    if (action == "cancelMusic")
    {
        aiMusic.cancel();
        return state();
    }
    if (action == "stopServer")
    {
        aiMusic.stopServer();
        return state();
    }
    if (action == "chat")
    {
        if (auto sent = aiSend(args["message"].toString()); sent.failed())
            throw McpFailure(sent.getErrorMessage().upToFirstOccurrenceOf(":", false, false), sent.getErrorMessage());
        auto o = state();
        set(o, "completionState", "pending");
        return o;
    }
    if (action == "events")
    {
        const auto since = juce::jmax(0, int(args.getProperty("since", 0)));
        const auto events = aiEvents();
        juce::Array<juce::var> rows;
        const juce::StringArray kinds{"user", "assistant", "tool", "error", "status"};
        for (size_t i = size_t(since); i < events.size(); ++i)
        {
            auto row = obj();
            set(row, "index", juce::int64(i));
            set(row, "kind", kinds[int(events[i].kind)]);
            set(row, "text", events[i].text);
            rows.add(row);
        }
        auto o = obj();
        set(o, "events", rows);
        set(o, "next", juce::int64(events.size()));
        set(o, "busy", aiBusy());
        return o;
    }
    if (action == "cancelChat")
    {
        aiCancel();
        return state();
    }
    if (action == "resetChat")
    {
        if (aiBusy())
            throw McpFailure("busy", "Stop the producer before starting a new chat");
        aiReset();
        return state();
    }
    if (action == "detectAgents")
    {
        aiDetectAgents(); // Spawns both CLIs: never on the message thread; poll state.agent.detecting.
        return state();
    }
    if (action == "panel")
    {
        const auto tab = args["tab"].toString();
        if (tab.isNotEmpty() && tab != "chat" && tab != "midi")
            throw McpFailure("invalid_arguments", "tab must be chat or midi");
        const bool visible = args.hasProperty("visible") ? bool(args["visible"]) : true;
        if (visible)
            showCoProducer(tab == "midi" ? 1 : tab == "chat" ? 0 : -1);
        else
            hideCoProducer();
        return state();
    }
    throw McpFailure("unsupported_operation", "Unknown vibera_ai action");
}
} // namespace vibera
