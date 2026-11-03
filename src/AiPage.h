// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Studio.h"

namespace vibera
{
// Settings > AI: language-model provider (cloud key or local server), local
// music engines (automatic install/start) and producer permissions.
class AiPage final : public juce::Component, private juce::Timer
{
    Studio &studio;
    juce::Label modelHeading, providerLabel, urlLabel, modelLabel, keyLabel, effortLabel, connection;
    juce::ComboBox provider, model, effort;
    juce::TextEditor url, key;
    juce::TextButton fetch{"Fetch models"}, saveKey{"Save key"}, clearKey{"Clear key"}, test{"Test connection"};
    juce::Label musicHeading, aceLabel, aceStatus, yueLabel, yueStatus, yueNotice, job;
    juce::TextEditor aceUrl;
    juce::Label rootLabel, rootPath, gpuInfo, aceModelLabel, aceLmLabel;
    juce::ComboBox aceModel, aceLm;
    juce::TextButton rootBrowse{"Change..."};
    std::unique_ptr<juce::FileChooser> rootChooser;
    juce::ToggleButton aceAuto{"Start ACE-Step automatically"}, yueLicense{"I accept the YuE2 weights license"},
        yueLowVram{"YuE2 low-VRAM mode (8 GB budget)"};
    juce::TextButton aceInstall{"Install / start ACE-Step"}, aceStop{"Stop server"}, yueInstall{"Install / check YuE2"},
        cancelJob{"Cancel"};
    juce::Label agentHeading, stepsLabel;
    juce::ToggleButton mayEdit{"CoProducer may edit the project"};
    juce::Slider steps;
    juce::TextButton openProducer{"Open CoProducer"};
    bool updating = false;
    juce::String shownModels;
    juce::StringArray effortIds;

    ai::AiSettings &settings()
    {
        return studio.aiSettings;
    }
    void save()
    {
        studio.saveAiSettings();
    }
    void syncProvider()
    {
        updating = true;
        const auto *preset = ai::findPreset(settings().provider.preset);
        provider.setSelectedId(1 + int(preset - ai::providerPresets().data()), juce::dontSendNotification);
        url.setText(settings().provider.baseUrl, false);
        url.setTextToShowWhenEmpty(preset ? preset->baseUrl : juce::String{}, juce::Colours::grey);
        model.setText(settings().effectiveModel(), juce::dontSendNotification);
        const bool anthropic = settings().dialect() == ai::Dialect::anthropic;
        const bool claudeCli = settings().dialect() == ai::Dialect::claudeCli;
        const bool codex = settings().dialect() == ai::Dialect::codexCli || claudeCli;
        syncEffort();
        effort.setVisible(anthropic || codex);
        effortLabel.setVisible(anthropic || codex);
        // The subscription provider needs no URL or key: hide those rows instead of showing them empty.
        for (auto *c : std::initializer_list<juce::Component *>{&urlLabel, &url, &keyLabel, &key, &saveKey, &clearKey})
            c->setVisible(!codex);
        fetch.setVisible(!codex);
        if (codex)
            studio.aiTestConnection(); // Shows "codex-cli x - Logged in using ChatGPT" asynchronously.
        resized();
        test.setButtonText(claudeCli ? "Check Claude sign-in" : codex ? "Check Codex sign-in" : "Test connection");
        model.setTextWhenNothingSelected(claudeCli ? "Claude Code default model" : codex ? "Codex default model" : "");
        const bool needsKey = preset && preset->needsKey;
        key.setEnabled(needsKey);
        saveKey.setEnabled(needsKey);
        clearKey.setEnabled(needsKey && studio.aiSecrets().has(settings().provider.preset));
        key.setText({}, false);
        key.setTextToShowWhenEmpty(codex ? "No key: uses your subscription through the signed-in CLI"
                                   : !needsKey                                            ? "No key needed for a local server"
                                   : studio.aiSecrets().has(settings().provider.preset) ? "Key saved (encrypted) - type to replace"
                                                                                        : "Paste your API key",
                                   juce::Colours::grey);
        updating = false;
    }
    // Codex offers the levels of the chosen model (plus its default); Anthropic offers low..max.
    void syncEffort()
    {
        const bool codex = settings().dialect() == ai::Dialect::codexCli;
        const bool claudeCli = settings().dialect() == ai::Dialect::claudeCli;
        effortIds.clear();
        effort.clear(juce::dontSendNotification);
        juce::StringArray levels{"low", "medium", "high", "xhigh", "max"};
        if (codex)
        {
            const auto known = ai::knownCodex();
            const auto *chosen = known ? known->find(settings().provider.model) : nullptr;
            if (chosen)
                levels = chosen->efforts;
            effortIds.add({});
            effort.addItem(chosen && chosen->defaultEffort.isNotEmpty() ? "Model default (" + chosen->defaultEffort + ")"
                                                                       : juce::String("Model default"),
                           1);
        }
        else if (claudeCli)
        {
            effortIds.add({});
            effort.addItem("Default", 1);
        }
        for (const auto &level : levels)
        {
            effortIds.add(level);
            effort.addItem(level, effortIds.size());
        }
        const auto index = effortIds.indexOf(settings().provider.effort);
        effort.setSelectedId(index >= 0 ? index + 1 : codex || claudeCli ? 1 : 3, juce::dontSendNotification);
    }
    void timerCallback() override
    {
        {
            std::lock_guard<std::mutex> lock(studio.aiConnection.mutex);
            connection.setText(studio.aiConnection.message, juce::dontSendNotification);
            const auto joined = studio.aiConnection.models.joinIntoString("\n");
            if (joined != shownModels && !studio.aiConnection.running.load())
            {
                shownModels = joined;
                const auto current = model.getText();
                model.clear(juce::dontSendNotification);
                for (int i = 0; i < studio.aiConnection.models.size(); ++i)
                    model.addItem(studio.aiConnection.models[i], i + 1);
                model.setText(current, juce::dontSendNotification);
                updating = true;
                syncEffort();
                updating = false;
            }
        }
        const auto &m = settings().music;
        {
            const auto root = ai::managedAiRoot(m);
            auto existing = root;
            while (!existing.exists() && existing.getParentDirectory() != existing)
                existing = existing.getParentDirectory();
            const auto free = existing.getBytesFreeOnVolume();
            rootPath.setText(root.getFullPathName() + (m.engineRoot == juce::File{} ? "  (automatic)" : "") +
                                 (free > 0 ? "  -  " + juce::String(free >> 30) + " GB free" : juce::String{}),
                             juce::dontSendNotification);
        }
        aceStatus.setText(juce::String(ai::MusicEngines::aceStepInstalled(m) ? "Installed" : "Not installed (installs on first use)") +
                              (studio.aiMusic.serverRunning() ? " - server started by VibeRa" : ""),
                          juce::dontSendNotification);
        yueStatus.setText(ai::MusicEngines::yueInstalled(m) ? "Installed" : "Not installed (installs on first use)",
                          juce::dontSendNotification);
        const auto snapshot = studio.aiMusic.snapshot();
        juce::String text;
        if (snapshot.state == ai::JobState::running)
            text << snapshot.engine << ": " << snapshot.stage
                 << (snapshot.progress >= 0 ? " " + juce::String(int(snapshot.progress * 100)) + "%" : juce::String("..."));
        else if (snapshot.state != ai::JobState::idle)
            text << snapshot.engine << ": " << snapshot.message;
        job.setText(text, juce::dontSendNotification);
        cancelJob.setEnabled(snapshot.state == ai::JobState::running);
        test.setEnabled(!studio.aiConnection.running.load());
    }
    void heading(juce::Label &label, const juce::String &text)
    {
        label.setText(text, juce::dontSendNotification);
        label.setFont(juce::FontOptions(17.f, juce::Font::bold));
        addAndMakeVisible(label);
    }
    void caption(juce::Label &label, const juce::String &text)
    {
        label.setText(text, juce::dontSendNotification);
        addAndMakeVisible(label);
    }

  public:
    explicit AiPage(Studio &s) : studio(s)
    {
        heading(modelHeading, "Language model (CoProducer brain)");
        caption(providerLabel, "Provider");
        caption(urlLabel, "Base URL");
        caption(modelLabel, "Model");
        caption(keyLabel, "API key");
        caption(effortLabel, "Effort");
        for (size_t i = 0; i < ai::providerPresets().size(); ++i)
            provider.addItem(ai::providerPresets()[i].label, int(i) + 1);
        provider.onChange = [this]
        {
            if (updating || provider.getSelectedId() <= 0)
                return;
            settings().provider = {ai::providerPresets()[size_t(provider.getSelectedId() - 1)].id};
            save();
            shownModels.clear();
            syncProvider();
        };
        url.onFocusLost = url.onReturnKey = [this]
        {
            settings().provider.baseUrl = url.getText().trim();
            save();
        };
        model.setEditableText(true);
        model.onChange = [this]
        {
            if (updating)
                return;
            settings().provider.model = model.getText().trim();
            save();
            updating = true;
            syncEffort(); // Codex models differ in the efforts they accept.
            updating = false;
        };
        effort.onChange = [this]
        {
            if (updating || effort.getSelectedId() <= 0)
                return;
            settings().provider.effort = effortIds[effort.getSelectedId() - 1];
            save();
        };
        key.setPasswordCharacter(juce::juce_wchar(0x2022));
        for (auto *editor : {&url, &key, &aceUrl})
        {
            editor->setFont(juce::Font(vibera::uiFont, 13.f, juce::Font::plain));
            editor->setIndents(6, 6);
        }
        saveKey.onClick = [this]
        {
            if (key.getText().trim().isEmpty())
                return;
            const auto stored = studio.aiSecrets().set(settings().provider.preset, key.getText().trim());
            connection.setText(stored.wasOk() ? "Key saved, encrypted for this Windows user" : stored.getErrorMessage(),
                               juce::dontSendNotification);
            syncProvider();
        };
        clearKey.onClick = [this]
        {
            studio.aiSecrets().set(settings().provider.preset, {});
            syncProvider();
        };
        fetch.onClick = test.onClick = [this] { studio.aiTestConnection(); };
        for (auto *c : std::initializer_list<juce::Component *>{&provider, &url, &model, &key, &effort, &fetch,
                                                                &saveKey, &clearKey, &test, &connection})
            addAndMakeVisible(c);

        heading(musicHeading, "Music generation (local)");
        caption(rootLabel, "Engines folder");
        rootBrowse.onClick = [this]
        {
            rootChooser = std::make_unique<juce::FileChooser>("Folder for AI engines and models (needs 40-70 GB)",
                                                              ai::managedAiRoot(settings().music));
            rootChooser->launchAsync(juce::FileBrowserComponent::openMode |
                                         juce::FileBrowserComponent::canSelectDirectories,
                                     [this](const juce::FileChooser &chooser)
                                     {
                                         if (chooser.getResult() == juce::File{})
                                             return;
                                         settings().music.engineRoot = chooser.getResult();
                                         save();
                                     });
        };
        caption(aceLabel, "ACE-Step 1.5 (MIT) - API URL");
        {
            const auto &gpu = ai::cachedGpu();
            const auto rec = ai::recommendAceStep(gpu.vramMb);
            const auto ditName = [](const juce::String &id)
            {
                return id.replace("acestep-v15-", "").replace("xl-", "XL ").replace("turbo", "Turbo")
                    .replace("sft", "SFT").replace("base", "Base");
            };
            const auto lmName = [](const juce::String &id)
            { return id == "none" ? juce::String("None") : id.replace("acestep-5Hz-lm-", "") + " planner"; };
            gpuInfo.setText((gpu.nvidia ? gpu.name.replace("NVIDIA GeForce ", "") + ", " +
                                              juce::String(juce::roundToInt(gpu.vramMb / 1024.0)) + " GB VRAM"
                                        : juce::String("No NVIDIA GPU detected")) +
                                "  ->  recommended " + ditName(rec.dit) + " + " + lmName(rec.lm) +
                                (rec.offloadToCpu ? " (CPU offload)" : ""),
                            juce::dontSendNotification);
            gpuInfo.setTooltip(rec.reason);
            gpuInfo.setMinimumHorizontalScale(1.f);
            addAndMakeVisible(gpuInfo);
            caption(aceModelLabel, "Music model");
            caption(aceLmLabel, "Planner LM");
            aceModel.addItem("Auto (" + ditName(rec.dit) + ")", 1);
            for (int i = 0; i < ai::aceStepDitModels().size(); ++i)
                aceModel.addItem(ditName(ai::aceStepDitModels()[i]), i + 2);
            aceLm.addItem("Auto (" + lmName(rec.lm) + ")", 1);
            for (int i = 0; i < ai::aceStepLmModels().size(); ++i)
                aceLm.addItem(lmName(ai::aceStepLmModels()[i]), i + 2);
            const auto ditIndex = ai::aceStepDitModels().indexOf(settings().music.aceDit);
            aceModel.setSelectedId(ditIndex < 0 ? 1 : ditIndex + 2, juce::dontSendNotification);
            const auto lmIndex = ai::aceStepLmModels().indexOf(settings().music.aceLm);
            aceLm.setSelectedId(lmIndex < 0 ? 1 : lmIndex + 2, juce::dontSendNotification);
            aceModel.setTooltip("turbo: fast (8 steps). sft/base: more steps, base supports guidance. XL (4B): "
                                "higher quality, needs 12 GB+ (CPU offload below 20 GB). Applied on the next "
                                "server start; VibeRa restarts its own server when you change it.");
            aceModel.onChange = [this]
            {
                const auto id = aceModel.getSelectedId();
                settings().music.aceDit = id <= 1 ? juce::String{} : ai::aceStepDitModels()[id - 2];
                save();
            };
            aceLm.onChange = [this]
            {
                const auto id = aceLm.getSelectedId();
                settings().music.aceLm = id <= 1 ? juce::String{} : ai::aceStepLmModels()[id - 2];
                save();
            };
            addAndMakeVisible(aceModel);
            addAndMakeVisible(aceLm);
        }
        aceUrl.setText(settings().music.aceStepUrl, false);
        aceUrl.onFocusLost = aceUrl.onReturnKey = [this]
        {
            if (aceUrl.getText().startsWith("http"))
                settings().music.aceStepUrl = aceUrl.getText().trim();
            save();
        };
        aceAuto.setToggleState(settings().music.aceStepAutoStart, juce::dontSendNotification);
        aceAuto.onClick = [this]
        {
            settings().music.aceStepAutoStart = aceAuto.getToggleState();
            save();
        };
        aceInstall.onClick = [this]
        {
            if (auto started = studio.aiMusic.prepare("ace-step", settings().music); started.failed())
                job.setText(started.getErrorMessage(), juce::dontSendNotification);
        };
        aceStop.onClick = [this] { studio.aiMusic.stopServer(); };
        caption(yueLabel, "YuE2 (lyrics-to-song)");
        yueLabel.setFont(juce::Font(vibera::uiFont, 14.f, juce::Font::bold));
        caption(yueNotice, "YuE2 weights are CC BY-NC 4.0: personal creators may monetize outputs; companies need a "
                           "commercial license from the authors. Needs an NVIDIA GPU (8 GB+ with low-VRAM mode).");
        yueNotice.setMinimumHorizontalScale(1.f);
        yueLicense.setToggleState(settings().music.yueLicenseAccepted, juce::dontSendNotification);
        yueLicense.onClick = [this]
        {
            settings().music.yueLicenseAccepted = yueLicense.getToggleState();
            save();
        };
        yueLowVram.setToggleState(settings().music.yueLowVram, juce::dontSendNotification);
        yueLowVram.onClick = [this]
        {
            settings().music.yueLowVram = yueLowVram.getToggleState();
            save();
        };
        yueInstall.onClick = [this]
        {
            if (auto started = studio.aiMusic.prepare("yue2", settings().music); started.failed())
                job.setText(started.getErrorMessage(), juce::dontSendNotification);
        };
        cancelJob.onClick = [this] { studio.aiMusic.cancel(); };
        for (auto *c : std::initializer_list<juce::Component *>{&rootPath, &rootBrowse, &aceUrl, &aceAuto, &aceStatus,
                                                                &aceInstall, &aceStop,
                                                                &yueLicense, &yueLowVram, &yueStatus, &yueInstall,
                                                                &job, &cancelJob})
            addAndMakeVisible(c);

        heading(agentHeading, "CoProducer");
        mayEdit.setToggleState(settings().agentMayEditProject, juce::dontSendNotification);
        mayEdit.onClick = [this]
        {
            settings().agentMayEditProject = mayEdit.getToggleState();
            save();
        };
        caption(stepsLabel, "Max steps");
        stepsLabel.setTooltip("Maximum tool calls the producer may make for one message");
        steps.setRange(1, 200, 1);
        steps.setValue(settings().maxAgentSteps, juce::dontSendNotification);
        steps.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 24);
        steps.onValueChange = [this]
        {
            settings().maxAgentSteps = int(steps.getValue());
            save();
        };
        openProducer.onClick = [this] { studio.showCoProducer(0); };
        for (auto *c : std::initializer_list<juce::Component *>{&mayEdit, &steps, &openProducer})
            addAndMakeVisible(c);
        syncProvider();
        setSize(640, 870);
        startTimerHz(4);
        timerCallback();
    }
    void resized() override
    {
        const int w = getWidth() - 36, x = 18, field = 130;
        int y = 12;
        auto row = [&](juce::Label &label, juce::Component &c, int height = 28)
        {
            label.setBounds(x, y, field - 6, height);
            c.setBounds(x + field, y, w - field, height);
            y += height + 8;
        };
        modelHeading.setBounds(x, y, w, 26);
        y += 34;
        row(providerLabel, provider);
        if (url.isVisible())
            row(urlLabel, url);
        modelLabel.setBounds(x, y, field - 6, 28);
        model.setBounds(x + field, y, fetch.isVisible() ? w - field - 130 : w - field, 28);
        fetch.setBounds(x + w - 122, y, 122, 28);
        y += 36;
        if (key.isVisible())
        {
            keyLabel.setBounds(x, y, field - 6, 28);
            key.setBounds(x + field, y, w - field - 196, 28);
            saveKey.setBounds(x + w - 188, y, 90, 28);
            clearKey.setBounds(x + w - 92, y, 92, 28);
            y += 36;
        }
        if (effort.isVisible())
            row(effortLabel, effort);
        test.setBounds(x, y, 150, 28);
        connection.setBounds(x + 160, y, w - 160, 28);
        y += 48;
        musicHeading.setBounds(x, y, w, 26);
        y += 34;
        rootLabel.setBounds(x, y, field - 6, 28);
        rootPath.setBounds(x + field, y, w - field - 100, 28);
        rootBrowse.setBounds(x + w - 92, y, 92, 28);
        y += 36;
        gpuInfo.setBounds(x, y, w, 28);
        y += 34;
        aceModelLabel.setBounds(x, y, field - 6, 28);
        aceModel.setBounds(x + field, y, (w - field) / 2 - 4, 28);
        aceLmLabel.setBounds(x + field + (w - field) / 2 + 4, y, 90, 28);
        aceLm.setBounds(x + field + (w - field) / 2 + 96, y, (w - field) / 2 - 96, 28);
        y += 36;
        aceLabel.setBounds(x, y, 220, 28);
        aceUrl.setBounds(x + 226, y, w - 226, 28);
        y += 36;
        aceAuto.setBounds(x, y, 260, 28);
        aceStatus.setBounds(x + 270, y, w - 270, 28);
        y += 36;
        aceInstall.setBounds(x, y, 200, 28);
        aceStop.setBounds(x + 208, y, 110, 28);
        y += 44;
        yueLabel.setBounds(x, y, w, 26);
        y += 28;
        yueNotice.setBounds(x, y, w, 44);
        y += 48;
        yueLicense.setBounds(x, y, 300, 28);
        yueStatus.setBounds(x + 310, y, w - 310, 28);
        y += 36;
        yueLowVram.setBounds(x, y, 300, 28);
        yueInstall.setBounds(x + w - 190, y, 190, 28);
        y += 40;
        job.setBounds(x, y, w - 100, 28);
        cancelJob.setBounds(x + w - 90, y, 90, 28);
        y += 48;
        agentHeading.setBounds(x, y, w, 26);
        y += 34;
        mayEdit.setBounds(x, y, w, 28);
        y += 36;
        row(stepsLabel, steps);
        openProducer.setBounds(x, y, 200, 30);
    }
};
} // namespace vibera
