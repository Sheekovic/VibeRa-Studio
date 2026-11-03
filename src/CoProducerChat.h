// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Studio.h"
#include "ToolbarButton.h"

namespace vibera
{
// CoProducer chat. The producer edits the project through canonical MCP tools
// (each edit is a normal Undo step) and can generate audio with ACE-Step/YuE2.
// Model and reasoning effort are chosen in the composer, like an IDE agent chat.
class CoProducerChat final : public juce::Component, private juce::Timer
{
  public:
    // Painted message list: user bubbles on the right, producer replies as plain text,
    // tool calls as compact rows. Measures its own height for the enclosing Viewport.
    class Transcript final : public juce::Component
    {
      public:
        struct Entry
        {
            ai::AgentEvent::Kind kind;
            juce::String text;
        };
        std::vector<Entry> entries;
        juce::String intro;
        std::function<void(const juce::String &)> onSuggestion;
        const juce::StringArray suggestions{"Make a 90 BPM lo-fi beat in A minor with drums, bass and keys",
                                            "Write a bassline that follows the current chords",
                                            "Write lyrics and generate a vocal hook with ACE-Step"};

        int layout(int width)
        {
            blocks.clear();
            chips.clear();
            const float w = float(std::max(200, width)), pad = 14.f;
            float y = 14.f;
            if (entries.empty())
            {
                y += 24.f;
                welcome = {pad, y, w - 2 * pad, 56.f};
                y += 64.f;
                for (int i = 0; i < suggestions.size(); ++i)
                {
                    auto text = make(suggestions[i], 13.f, ink, w - 2 * pad - 24);
                    const float h = text.getHeight() + 16.f;
                    chips.push_back({{pad, y, w - 2 * pad, h}, std::move(text), i});
                    y += h + 8.f;
                }
                return int(y + 14.f);
            }
            for (size_t i = 0; i < entries.size(); ++i)
            {
                const auto &e = entries[i];
                Block block;
                block.entry = int(i);
                block.kind = e.kind;
                if (e.kind == ai::AgentEvent::Kind::user)
                {
                    block.text = make(e.text, 13.5f, ink, w * .82f - 24);
                    const float bw = std::min(w * .82f, widthOf(block.text) + 24.f);
                    block.box = {w - pad - bw, y, bw, block.text.getHeight() + 18.f};
                }
                else if (e.kind == ai::AgentEvent::Kind::assistant)
                {
                    y += 2.f;
                    block.text = make(e.text, 13.5f, ink, w - 2 * pad);
                    block.box = {pad, y, w - 2 * pad, block.text.getHeight() + 22.f};
                }
                else if (e.kind == ai::AgentEvent::Kind::tool)
                {
                    block.text = make(e.text.length() > 180 ? e.text.substring(0, 180) + "..." : e.text, 11.5f,
                                      muted, w - 2 * pad - 30, true);
                    block.box = {pad, y, w - 2 * pad, block.text.getHeight() + 12.f};
                }
                else
                {
                    block.text = make(e.text, 12.5f, juce::Colour(0xffffb4bd), w - 2 * pad - 24);
                    block.box = {pad, y, w - 2 * pad, block.text.getHeight() + 18.f};
                }
                y += block.box.getHeight() + (e.kind == ai::AgentEvent::Kind::tool ? 4.f : 12.f);
                blocks.push_back(std::move(block));
            }
            return int(y + 6.f);
        }
        void paint(juce::Graphics &g) override
        {
            if (entries.empty())
            {
                g.setColour(ink);
                g.setFont(juce::FontOptions(vibera::uiFont, 19.f, juce::Font::bold));
                g.drawText("What are we making?", welcome.withHeight(26.f), juce::Justification::centredLeft);
                g.setColour(muted);
                g.setFont(juce::FontOptions(vibera::uiFont, 12.5f, juce::Font::plain));
                g.drawFittedText(intro, welcome.withTrimmedTop(28.f).toNearestInt(), juce::Justification::topLeft,
                                 2);
                for (const auto &chip : chips)
                {
                    const bool over = chip.index == hoverChip;
                    g.setColour(over ? accent.withAlpha(.12f) : panel.brighter(.04f));
                    g.fillRoundedRectangle(chip.box, 7.f);
                    g.setColour(over ? accent.withAlpha(.6f) : grid);
                    g.drawRoundedRectangle(chip.box.reduced(.5f), 7.f, 1.f);
                    chip.text.draw(g, chip.box.reduced(12.f, 8.f));
                }
                return;
            }
            for (const auto &b : blocks)
                switch (b.kind)
                {
                case ai::AgentEvent::Kind::user:
                    g.setColour(accent.withAlpha(.16f));
                    g.fillRoundedRectangle(b.box, 10.f);
                    g.setColour(accent.withAlpha(.35f));
                    g.drawRoundedRectangle(b.box.reduced(.5f), 10.f, 1.f);
                    b.text.draw(g, b.box.reduced(12.f, 9.f));
                    break;
                case ai::AgentEvent::Kind::assistant:
                    g.setColour(accent);
                    g.setFont(juce::FontOptions(vibera::uiFont, 11.5f, juce::Font::bold));
                    g.drawText("CoProducer", b.box.withHeight(16.f), juce::Justification::centredLeft);
                    b.text.draw(g, b.box.withTrimmedTop(20.f));
                    break;
                case ai::AgentEvent::Kind::tool:
                {
                    g.setColour(bg.withAlpha(.7f));
                    g.fillRoundedRectangle(b.box, 5.f);
                    juce::Path tick;
                    const auto c = b.box.getTopLeft().translated(13.f, 11.f);
                    tick.startNewSubPath(c.translated(-3.f, -3.f));
                    tick.lineTo(c.translated(1.f, 0.f));
                    tick.lineTo(c.translated(-3.f, 3.f));
                    g.setColour(muted);
                    g.strokePath(tick, juce::PathStrokeType(1.4f));
                    b.text.draw(g, b.box.reduced(24.f, 6.f).withTrimmedRight(-18.f));
                    break;
                }
                case ai::AgentEvent::Kind::error:
                case ai::AgentEvent::Kind::status:
                    g.setColour(juce::Colour(0xffef6576).withAlpha(.12f));
                    g.fillRoundedRectangle(b.box, 7.f);
                    g.setColour(juce::Colour(0xffef6576).withAlpha(.55f));
                    g.drawRoundedRectangle(b.box.reduced(.5f), 7.f, 1.f);
                    b.text.draw(g, b.box.reduced(12.f, 9.f));
                    break;
                }
        }
        void mouseMove(const juce::MouseEvent &e) override
        {
            int over = -1;
            for (const auto &chip : chips)
                if (chip.box.contains(e.position))
                    over = chip.index;
            if (over != hoverChip)
            {
                hoverChip = over;
                setMouseCursor(over >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
                repaint();
            }
        }
        void mouseExit(const juce::MouseEvent &) override
        {
            hoverChip = -1;
            repaint();
        }
        void mouseUp(const juce::MouseEvent &e) override
        {
            if (e.mods.isPopupMenu())
            {
                const Block *hit = nullptr;
                for (const auto &b : blocks)
                    if (b.box.contains(e.position))
                        hit = &b;
                juce::PopupMenu menu;
                menu.addItem(1, "Copy message", hit != nullptr);
                menu.addItem(2, "Copy conversation", !entries.empty());
                const auto message = hit ? entries[size_t(hit->entry)].text : juce::String{};
                menu.showMenuAsync(juce::PopupMenu::Options(),
                                   [this, message](int choice)
                                   {
                                       if (choice == 1)
                                           juce::SystemClipboard::copyTextToClipboard(message);
                                       if (choice == 2)
                                           juce::SystemClipboard::copyTextToClipboard(plainText());
                                   });
                return;
            }
            for (const auto &chip : chips)
                if (chip.box.contains(e.position) && onSuggestion)
                    onSuggestion(suggestions[chip.index]);
        }
        juce::String plainText() const
        {
            juce::String all;
            for (const auto &e : entries)
                all << (e.kind == ai::AgentEvent::Kind::user        ? "You: "
                        : e.kind == ai::AgentEvent::Kind::assistant ? "CoProducer: "
                        : e.kind == ai::AgentEvent::Kind::tool      ? "  [tool] "
                                                                    : "! ")
                    << e.text << "\n";
            return all;
        }

      private:
        struct Block
        {
            juce::Rectangle<float> box;
            juce::TextLayout text;
            int entry = 0;
            ai::AgentEvent::Kind kind = ai::AgentEvent::Kind::assistant;
        };
        struct Chip
        {
            juce::Rectangle<float> box;
            juce::TextLayout text;
            int index = 0;
        };
        std::vector<Block> blocks;
        std::vector<Chip> chips;
        juce::Rectangle<float> welcome;
        int hoverChip = -1;
        static juce::TextLayout make(const juce::String &text, float size, juce::Colour colour, float width,
                                     bool mono = false)
        {
            juce::AttributedString s;
            s.setWordWrap(juce::AttributedString::byWord);
            s.setLineSpacing(size * .28f);
            s.append(text, juce::FontOptions(mono ? "Consolas" : vibera::uiFont, size, juce::Font::plain), colour);
            juce::TextLayout layout;
            layout.createLayout(s, std::max(40.f, width));
            return layout;
        }
        static float widthOf(const juce::TextLayout &layout)
        {
            float right = 0;
            for (int i = 0; i < layout.getNumLines(); ++i)
                right = std::max(right, layout.getLine(i).getLineBoundsX().getEnd());
            return right;
        }
    };

  private:
    Studio &studio;
    juce::Viewport scroller;
    Transcript transcript;
    juce::TextEditor input;
    juce::ComboBox model, effort;
    juce::TextButton send{"Send"};
    juce::Label progress;
    std::vector<juce::String> modelIds, effortIds;
    juce::String menuKey;
    size_t shown = 0;
    int pulse = 0;
    bool wasBusy = false, codexRequested = false;
    int composerLines = 2;

    int inputLines() const
    {
        return juce::jlimit(2, 7, int(std::ceil(input.getTextHeight() / 20.0)));
    }

    static juce::String effortName(const juce::String &id)
    {
        return id == "xhigh" ? juce::String("Extra high") : id.substring(0, 1).toUpperCase() + id.substring(1);
    }
    bool codex() const
    {
        return studio.aiSettings.dialect() == ai::Dialect::codexCli;
    }
    bool claudeCli() const
    {
        return studio.aiSettings.dialect() == ai::Dialect::claudeCli;
    }
    void submit()
    {
        if (studio.aiBusy() || studio.aiMusic.snapshot().state == ai::JobState::running)
        {
            studio.aiCancel();
            studio.aiMusic.cancel();
            return;
        }
        const auto text = input.getText().trim();
        if (text.isEmpty())
            return;
        if (auto sent = studio.aiSend(text); sent.failed())
        {
            progress.setText(sent.getErrorMessage().fromFirstOccurrenceOf(": ", false, false),
                             juce::dontSendNotification);
            return;
        }
        input.clear();
        resized();
        timerCallback();
    }
    // Rebuilds the model/effort pickers when the provider, catalog or choice changes.
    void syncPickers()
    {
        auto &provider = studio.aiSettings.provider;
        const auto known = codex() ? ai::knownCodex() : std::optional<ai::CodexStatus>{};
        juce::StringArray fetched;
        {
            std::lock_guard<std::mutex> lock(studio.aiConnection.mutex);
            fetched = studio.aiConnection.models;
        }
        juce::String key = provider.preset + "|" + provider.model + "|" + provider.effort + "|" +
                           fetched.joinIntoString(",") + "|" + juce::String(known ? int(known->models.size()) : -1);
        if (key == menuKey)
            return;
        menuKey = key;
        model.clear(juce::dontSendNotification);
        effort.clear(juce::dontSendNotification);
        modelIds.clear();
        effortIds.clear();
        juce::StringArray levels;
        if (claudeCli())
        {
            for (const auto &alias : ai::claudeCliModels())
            {
                modelIds.push_back(alias);
                model.addItem(alias.isEmpty() ? juce::String("Default model")
                                              : alias.substring(0, 1).toUpperCase() + alias.substring(1),
                              int(modelIds.size()));
            }
            levels = {"low", "medium", "high", "xhigh", "max"};
            effortIds.push_back({});
            effort.addItem("Default effort", 1);
        }
        else if (codex())
        {
            modelIds.push_back({});
            model.addItem("Default model", 1);
            if (known)
                for (const auto &m : known->models)
                {
                    modelIds.push_back(m.slug);
                    model.addItem(m.name, int(modelIds.size()));
                }
            juce::String defaultLevel;
            if (const auto *chosen = known ? known->find(provider.model) : nullptr)
            {
                levels = chosen->efforts;
                defaultLevel = chosen->defaultEffort;
            }
            else
                levels = {"low", "medium", "high", "xhigh", "max"};
            effortIds.push_back({});
            effort.addItem(defaultLevel.isNotEmpty() ? "Default (" + effortName(defaultLevel) + ")"
                                                     : juce::String("Default effort"),
                           1);
        }
        else
        {
            juce::StringArray names = fetched;
            if (studio.aiSettings.dialect() == ai::Dialect::anthropic && names.isEmpty())
                names = {"claude-opus-5-5", "claude-sonnet-5-5", "claude-haiku-5-5", "claude-fable-5-1"};
            const auto current = studio.aiSettings.effectiveModel();
            if (current.isNotEmpty() && !names.contains(current))
                names.insert(0, current);
            for (const auto &name : names)
            {
                modelIds.push_back(name);
                model.addItem(name, int(modelIds.size()));
            }
            if (studio.aiSettings.dialect() == ai::Dialect::anthropic)
                levels = {"low", "medium", "high", "xhigh", "max"};
        }
        for (const auto &level : levels)
        {
            effortIds.push_back(level);
            effort.addItem(effortName(level), int(effortIds.size()));
        }
        const auto chosenModel = codex() || claudeCli() ? provider.model : studio.aiSettings.effectiveModel();
        for (size_t i = 0; i < modelIds.size(); ++i)
            if (modelIds[i] == chosenModel)
                model.setSelectedId(int(i) + 1, juce::dontSendNotification);
        if (model.getSelectedId() == 0 && !modelIds.empty())
            model.setSelectedId(1, juce::dontSendNotification);
        for (size_t i = 0; i < effortIds.size(); ++i)
            if (effortIds[i] == provider.effort)
                effort.setSelectedId(int(i) + 1, juce::dontSendNotification);
        if (effort.getSelectedId() == 0 && !effortIds.empty())
            effort.setSelectedId(1, juce::dontSendNotification);
        effort.setVisible(!effortIds.empty());
        const auto *preset = ai::findPreset(provider.preset);
        model.setTooltip((preset ? preset->label : juce::String("Provider")) + " - model (change provider in Settings > AI)");
        effort.setTooltip("Reasoning effort: higher thinks longer and costs more time");
        resized();
    }
    void timerCallback() override
    {
        if (((codex() && !ai::knownCodex()) || (claudeCli() && !ai::knownClaudeCli())) && !codexRequested)
        {
            codexRequested = true;
            studio.aiTestConnection(); // Detects Codex (and its model catalog) off the message thread.
        }
        syncPickers();
        const auto events = studio.aiEvents();
        if (events.size() < shown)
        {
            transcript.entries.clear();
            shown = 0;
        }
        const bool added = shown < events.size();
        for (; shown < events.size(); ++shown)
            if (events[shown].kind != ai::AgentEvent::Kind::status) // Status shows in the progress line only.
                transcript.entries.push_back({events[shown].kind, events[shown].text});
        const auto *preset = ai::findPreset(studio.aiSettings.provider.preset);
        auto intro = "Using " + (preset ? preset->label : juce::String("no provider")) + ". " +
                     (studio.aiSettings.agentMayEditProject
                          ? "Edits are normal Undo steps; music generation runs locally."
                          : "Read-only: CoProducer can inspect but not edit the project.");
        if (intro != transcript.intro || added)
        {
            transcript.intro = intro;
            layoutTranscript(added);
        }
        const bool busy = studio.aiBusy();
        const auto music = studio.aiMusic.snapshot();
        const bool working = busy || music.state == ai::JobState::running;
        juce::String state;
        if (busy)
            for (auto it = events.rbegin(); it != events.rend(); ++it)
                if (it->kind == ai::AgentEvent::Kind::status)
                {
                    state = it->text.trimCharactersAtEnd(".");
                    break;
                }
        if (music.state == ai::JobState::running)
            state << (state.isEmpty() ? "" : "  |  ") << music.engine << ": " << music.stage
                  << (music.progress >= 0 ? " " + juce::String(int(music.progress * 100)) + "%" : juce::String{});
        if (working)
        {
            pulse = (pulse + 1) % 4;
            progress.setText((state.isEmpty() ? juce::String("Working") : state) + juce::String::repeatedString(".", pulse),
                             juce::dontSendNotification);
        }
        else if (wasBusy)
            progress.setText({}, juce::dontSendNotification);
        wasBusy = working;
        send.setButtonText(working ? "Stop" : "Send");
        // Primary action: drawn in the "on" colour (accent to send, red to stop).
        send.setColour(juce::TextButton::buttonOnColourId, working ? juce::Colour(0xff943c4b) : accent.darker(.3f));
        send.setToggleState(true, juce::dontSendNotification);
        model.setEnabled(!busy);
        effort.setEnabled(!busy);
        repaint(composerBounds().expanded(2).toNearestInt());
    }
    void layoutTranscript(bool scrollToEnd)
    {
        const int width = scroller.getMaximumVisibleWidth();
        transcript.setSize(width, std::max(scroller.getHeight(), transcript.layout(width)));
        transcript.repaint();
        if (scrollToEnd)
            scroller.setViewPosition(0, std::max(0, transcript.getHeight() - scroller.getHeight()));
    }
    juce::Rectangle<float> composerBounds() const
    {
        return input.getBounds().toFloat().expanded(10.f, 8.f).withBottom(float(send.getBottom()) + 8.f);
    }

  public:
    explicit CoProducerChat(Studio &s) : studio(s)
    {
        scroller.setViewedComponent(&transcript, false);
        scroller.setScrollBarsShown(true, false);
        scroller.setScrollBarThickness(8);
        transcript.onSuggestion = [this](const juce::String &text)
        {
            input.setText(text, false);
            input.grabKeyboardFocus();
            input.moveCaretToEnd();
        };
        input.setMultiLine(true, true);
        input.setReturnKeyStartsNewLine(false);
        input.setScrollbarsShown(true);
        input.setFont(juce::FontOptions(vibera::uiFont, 14.f, juce::Font::plain));
        input.setIndents(2, 4);
        input.setTextToShowWhenEmpty("Ask CoProducer to build, edit or generate...  (Shift+Enter: new line)", muted);
        input.setColour(juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
        input.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
        input.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
        input.onReturnKey = [this] { submit(); };
        input.onTextChange = [this]
        {
            if (inputLines() != composerLines)
                resized();
        };
        input.onFocusLost = [this] { repaint(); };
        send.onClick = [this] { submit(); };
        send.setColour(juce::TextButton::textColourOffId, ink);
        model.onChange = [this]
        {
            const auto index = model.getSelectedId() - 1;
            if (index < 0 || size_t(index) >= modelIds.size())
                return;
            studio.aiSettings.provider.model = modelIds[size_t(index)];
            studio.saveAiSettings();
            syncPickers();
        };
        effort.onChange = [this]
        {
            const auto index = effort.getSelectedId() - 1;
            if (index < 0 || size_t(index) >= effortIds.size())
                return;
            studio.aiSettings.provider.effort = effortIds[size_t(index)];
            studio.saveAiSettings();
        };
        progress.setFont(juce::FontOptions(vibera::uiFont, 12.f, juce::Font::plain));
        progress.setColour(juce::Label::textColourId, muted);
        for (auto *c : std::initializer_list<juce::Component *>{&scroller, &input, &model, &effort, &send, &progress})
            addAndMakeVisible(c);
        setSize(420, 700);
        startTimerHz(5);
        timerCallback();
    }
    void newChat()
    {
        if (studio.aiBusy())
            return;
        studio.aiReset();
        transcript.entries.clear();
        shown = 0;
        layoutTranscript(false);
        input.grabKeyboardFocus();
    }
    void paint(juce::Graphics &g) override
    {
        const auto box = composerBounds();
        g.setColour(bg);
        g.fillRoundedRectangle(box, 9.f);
        g.setColour(input.hasKeyboardFocus(true) ? accent.withAlpha(.7f) : grid.brighter(.15f));
        g.drawRoundedRectangle(box.reduced(.5f), 9.f, 1.f);
    }
    void resized() override
    {
        auto area = getLocalBounds();
        composerLines = inputLines();
        auto composer = area.removeFromBottom(composerLines * 20 + 62).reduced(12, 10);
        progress.setBounds(area.removeFromBottom(20).reduced(14, 0));
        scroller.setBounds(area);
        composer.reduce(10, 8);
        auto controls = composer.removeFromBottom(28);
        send.setBounds(controls.removeFromRight(68));
        controls.removeFromRight(8);
        const int pick = std::max(90, controls.getWidth());
        if (effort.isVisible())
        {
            model.setBounds(controls.removeFromLeft(int(pick * .58f)));
            controls.removeFromLeft(6);
            effort.setBounds(controls);
        }
        else
            model.setBounds(controls);
        composer.removeFromBottom(4);
        input.setBounds(composer);
        layoutTranscript(false);
        repaint();
    }
    Transcript &transcriptForTest()
    {
        return transcript;
    }
};
} // namespace vibera
