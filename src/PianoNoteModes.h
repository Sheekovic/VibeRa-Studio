// SPDX-License-Identifier: AGPL-3.0-or-later
#include "NumberText.h"
#pragma once
#include "McpContracts.h"
#include "McpSchema.h"
#include <charconv>
#include <set>

namespace vibera
{
inline juce::var pianoNoteValues(const Note &note)
{
    auto result = juce::var(new juce::DynamicObject);
    auto &value = *result.getDynamicObject();
    value.setProperty("slide", note.slide);
    value.setProperty("portamento", note.portamento);
    value.setProperty("colourGroup", note.colourGroup);
    value.setProperty("velocity", double(note.velocity));
    value.setProperty("pan", note.pan);
    value.setProperty("fineCents", note.fineCents);
    value.setProperty("modX", note.modX);
    value.setProperty("modY", note.modY);
    value.setProperty("releaseVelocity", note.releaseVelocity);
    value.setProperty("beat", note.beat);
    value.setProperty("length", note.length);
    value.setProperty("pitch", note.pitch);
    value.setProperty("muted", note.muted);
    return result;
}

inline juce::var pianoNoteModesToolDefinition()
{
    auto tool = juce::JSON::parse(R"JSON({"name":"vibera_note_modes",
      "description":"Shared Piano Roll insertion modes,16 note-color/MIDI groups and captured Note Properties. Configure changes session insertion defaults, not score/history. changeColour edits explicit/current selected source IDs (all current Channel notes when selection is empty); selectColour changes selection only. propertiesOpen captures exact project/revision/Pattern/Channel/IDs. propertiesUpdate stages a sparse patch; Reset restores its initial values, Cancel preserves score. Apply uses one atomic canonical notes.properties command/Undo. Source metadata is not a DSP acknowledgement. Slide/Porta/Pan/Fine/Mod X/Y require a local sampler; third-party native widgets/transitions are unsupported. Ordinary note colors reach supported hosted instruments as MIDI channels1..16.",
      "inputSchema":{"type":"object","additionalProperties":false,
      "properties":{"projectIdentity":{"type":"string","maxLength":100},"action":{"type":"string","enum":["state","configure","changeColour","selectColour","propertiesOpen","propertiesState","propertiesUpdate","propertiesReset","propertiesApply","propertiesCancel"]},
      "slide":{"type":"boolean","default":false},"portamento":{"type":"boolean","default":false},
      "colourGroup":{"type":"integer","minimum":0,"maximum":15,"default":0,"description":"Stable note-color index0..15; displayed1..16. Distinct from DAW Channel ID and selection-group ID."},
      "pattern":{"type":"integer","minimum":1,"maximum":1000000000},
      "channel":{"type":"integer","minimum":1,"maximum":1000000000},
      "ids":{"type":"array","minItems":1,"maxItems":100000,"uniqueItems":true,"items":{"type":"integer","minimum":1,"maximum":1000000000}},
      "operation":{"type":"string","minLength":1,"maxLength":100,"description":"Exact properties operation token, required after open; stale windows cannot affect a newer operation."},
      "noteProperties":{"type":"object"}},"required":["action"]}})JSON");
    tool["inputSchema"]["properties"].getDynamicObject()->setProperty(
        "noteProperties", commandArgumentSchema("notes.properties")["properties"]["noteProperties"]);
    tool["inputSchema"]["properties"]["noteProperties"].getDynamicObject()->setProperty("minProperties", 0);
    tool.getDynamicObject()->setProperty("outputSchema", juce::JSON::parse(R"JSON({
      "type":"object","additionalProperties":true,"required":["completionState"],
      "properties":{"completionState":{"type":"string","enum":["pending","completed","failed","cancelled"]},
      "completionScope":{"type":"string"},"operation":{"type":"string"},"documentChanged":{"type":"boolean"},
      "projectIdentity":{"type":"string"},"revision":{"type":"integer","minimum":0},
      "pattern":{"type":"integer","minimum":0},"channel":{"type":"integer","minimum":0},
      "slide":{"type":"boolean"},"portamento":{"type":"boolean"},
      "effectiveSlide":{"type":"boolean"},"effectivePortamento":{"type":"boolean"},
      "colourGroup":{"type":"integer","minimum":0,"maximum":15},"midiChannel":{"type":"integer","minimum":1,"maximum":16},
      "transitionSupported":{"type":"boolean"},"transitionAcknowledged":{"type":"null"},"nativePluginUiControl":{"type":"boolean","const":false},
      "properties":{"type":["object","null"],"additionalProperties":true,"properties":{
      "operation":{"type":"string"},"completionState":{"type":"string","enum":["pending","completed","cancelled"]},
      "projectIdentity":{"type":"string"},"revision":{"type":"integer","minimum":0},
      "pattern":{"type":"integer","minimum":1},"channel":{"type":"integer","minimum":1},
      "ids":{"type":"array","minItems":1,"uniqueItems":true,"items":{"type":"integer","minimum":1}},
      "baseline":{"type":"object"},"baselineNotes":{"type":"array","items":{"type":"object"}},
      "noteProperties":{"type":"object"},"transitionSupported":{"type":"boolean"},"timingAvailable":{"type":"boolean"},
      "stale":{"type":"boolean"},"visible":{"type":"boolean"},"error":{"type":"string"}}},
      "error":{"type":"object","additionalProperties":false,"required":["code","message"],
      "properties":{"code":{"type":"string"},"message":{"type":"string"}}}}})JSON"));
    return tool;
}

// Original functional controls; no proprietary graphics or native-input simulation.
class PianoNotePropertiesPanel final : public juce::Component
{
    struct Content final : juce::Component
    {
    } content;
    juce::Viewport viewport;
    juce::Label explanation, error;
    std::array<juce::Label, 9> labels;
    std::array<juce::TextEditor, 9> fields;
    juce::ToggleButton slide{"Slide control note"}, porta{"Porta inversion"}, muted{"Muted"};
    juce::ComboBox colour;
    juce::TextButton apply{"Apply"}, reset{"Reset"}, cancel{"Cancel"};
    juce::var initial;
    std::array<juce::String, 9> initialText;
    bool syncing = false;
    std::set<juce::String> touched;
    static constexpr std::array<const char *, 9> keys{"velocity",        "pan",  "fineCents", "modX", "modY",
                                                      "releaseVelocity", "beat", "length",    "pitch"};
    void changed(const char *key)
    {
        if (!syncing && edit)
        {
            touched.insert(key);
            juce::var patch;
            const auto result = read(patch);
            error.setText(result.failed() ? result.getErrorMessage() : juce::String{},
                          juce::dontSendNotification);
            if (result.wasOk())
                edit(patch);
        }
    }

  public:
    std::function<bool(const juce::var &)> edit;
    std::function<void(int)> finish;
    PianoNotePropertiesPanel()
    {
        setSize(490, 560);
        viewport.setViewedComponent(&content, false);
        viewport.setScrollBarsShown(true, false);
        viewport.setScrollBarThickness(7);
        addAndMakeVisible(viewport);
        content.addAndMakeVisible(explanation);
        explanation.setText("Only changed fields apply. Time / pitch editing requires one note.",
                            juce::dontSendNotification);
        explanation.setFont(juce::Font(13));
        for (auto *button : {&slide, &porta, &muted})
        {
            content.addAndMakeVisible(*button);
            button->onClick = [this, button]
            {
                changed(button == &slide ? "slide" : button == &porta ? "portamento" : "muted");
            };
        }
        slide.setComponentID("piano.noteProperties.slide");
        porta.setComponentID("piano.noteProperties.portamento");
        muted.setComponentID("piano.noteProperties.muted");
        content.addAndMakeVisible(colour);
        colour.setComponentID("piano.noteProperties.colourGroup");
        for (int index = 0; index < 16; ++index)
            colour.addItem("Color " + juce::String(index + 1) + " / MIDI " + juce::String(index + 1),
                           index + 1);
        colour.onChange = [this] { changed("colourGroup"); };
        const char *names[]{"Velocity (0..1)",
                            "Pan (-1..1)",
                            "Fine pitch (cents)",
                            "Mod X (0..1)",
                            "Mod Y (0..1)",
                            "Release velocity (0..1)",
                            "Start (quarter-note beats)",
                            "Length (quarter-note beats)",
                            "Pitch (MIDI0..127)"};
        for (size_t index = 0; index < 9; ++index)
        {
            content.addAndMakeVisible(labels[index]);
            labels[index].setText(names[index], juce::dontSendNotification);
            labels[index].setFont(juce::Font(13));
            content.addAndMakeVisible(fields[index]);
            fields[index].setComponentID("piano.noteProperties." + juce::String(keys[index]));
            fields[index].onTextChange = [this, index] { changed(keys[index]); };
            fields[index].onReturnKey = [this]
            {
                if (apply.onClick)
                    apply.onClick();
            };
            fields[index].onEscapeKey = [this]
            {
                if (finish)
                    finish(3);
            };
        }
        for (auto *button : {&apply, &reset, &cancel})
            addAndMakeVisible(*button);
        apply.setComponentID("piano.noteProperties.apply");
        reset.setComponentID("piano.noteProperties.reset");
        cancel.setComponentID("piano.noteProperties.cancel");
        apply.onClick = [this]
        {
            juce::var patch;
            const auto result = read(patch);
            if (result.failed())
            {
                setError(result.getErrorMessage());
                return;
            }
            if (edit && !edit(patch))
                return;
            if (finish)
                finish(1);
        };
        reset.onClick = [this]
        {
            if (finish)
                finish(2);
        };
        cancel.onClick = [this]
        {
            if (finish)
                finish(3);
        };
        addAndMakeVisible(error);
        error.setColour(juce::Label::textColourId, juce::Colour(0xffef9b8b));
        error.setFont(juce::Font(12));
    }
    bool hasEdits() const
    {
        return !touched.empty();
    }
    void setError(const juce::String &message)
    {
        error.setText(message, juce::dontSendNotification);
    }
    void sync(const juce::var &baseline, const juce::var &draft, bool local, bool single,
              const juce::var &baselineNotes)
    {
        const juce::ScopedValueSetter<bool> guard(syncing, true);
        initial = baseline.clone();
        touched.clear();
        if (draft.isObject())
            for (const auto &field : draft.getDynamicObject()->getProperties())
                touched.insert(field.name.toString());
        const auto current = [&](const char *key) { return draft.getProperty(key, baseline[key]); };
        const auto mixed = [&](const char *key)
        {
            if (draft.hasProperty(key))
                return false;
            if (const auto *notes = baselineNotes.getArray())
                for (const auto &note : *notes)
                    if (note[key] != baseline[key])
                        return true;
            return false;
        };
        slide.setButtonText(mixed("slide") ? "Slide (mixed)" : "Slide control note");
        porta.setButtonText(mixed("portamento") ? "Porta (mixed)" : "Porta inversion");
        muted.setButtonText(mixed("muted") ? "Muted (mixed)" : "Muted");
        slide.setToggleState(bool(current("slide")), juce::dontSendNotification);
        porta.setToggleState(bool(current("portamento")), juce::dontSendNotification);
        muted.setToggleState(bool(current("muted")), juce::dontSendNotification);
        slide.setEnabled(local);
        porta.setEnabled(local);
        colour.setTextWhenNothingSelected("Mixed colors");
        colour.setSelectedId(mixed("colourGroup") ? 0 : int(current("colourGroup")) + 1,
                             juce::dontSendNotification);
        for (size_t index = 0; index < 9; ++index)
        {
            initialText[index] = baseline[keys[index]].toString();
            const auto display = juce::String(double(current(keys[index])), index == 8 ? 0 : 6)
                                     .trimCharactersAtEnd("0")
                                     .trimCharactersAtEnd(".");
            fields[index].setText(mixed(keys[index]) ? juce::String("Mixed")
                                  : index == 8       ? current(keys[index]).toString()
                                                     : display,
                                  false);
            fields[index].setEnabled(index >= 6 ? single : (index == 0 || index == 5 || local));
        }
    }
    juce::Result read(juce::var &result) const
    {
        result = juce::var(new juce::DynamicObject);
        auto &patch = *result.getDynamicObject();
        for (const auto &[key, current] :
             std::array<std::pair<const char *, bool>, 3>{{{"slide", slide.getToggleState()},
                                                           {"portamento", porta.getToggleState()},
                                                           {"muted", muted.getToggleState()}}})
            if (touched.contains(key))
                patch.setProperty(key, current);
        if (touched.contains("colourGroup"))
            patch.setProperty("colourGroup", colour.getSelectedId() - 1);
        for (size_t index = 0; index < 9; ++index)
            if (fields[index].isEnabled() && touched.contains(keys[index]))
            {
                const auto text = fields[index].getText().trim().toStdString();
                double number = 0;
                const auto converted = vibera::number_text::fromChars(text.data(), text.data() + text.size(), number,
                                                       std::chars_format::general);
                if (text.empty() || converted.ec != std::errc{} ||
                    converted.ptr != text.data() + text.size() || !std::isfinite(number) ||
                    (index == 8 && (std::floor(number) != number || number < 0 || number > 127)))
                    return juce::Result::fail("Enter a finite " +
                                              juce::String(index == 8 ? "integer" : "number") + " for " +
                                              keys[index]);
                patch.setProperty(keys[index], index == 8 ? juce::var(int(number)) : juce::var(number));
            }
        if (!patch.getProperties().isEmpty())
        {
            NotePropertiesPatch decoded;
            return decodeNotePropertiesPatch(result, decoded);
        }
        return juce::Result::ok();
    }
    void resized() override
    {
        viewport.setBounds(8, 8, getWidth() - 16, std::max(40, getHeight() - 90));
        content.setSize(std::max(300, viewport.getMaximumVisibleWidth()), 430);
        explanation.setBounds(8, 0, content.getWidth() - 16, 40);
        const auto half = (content.getWidth() - 24) / 2;
        slide.setBounds(8, 42, half, 26);
        porta.setBounds(16 + half, 42, half, 26);
        muted.setBounds(8, 74, 130, 26);
        colour.setBounds(160, 74, content.getWidth() - 176, 26);
        for (size_t index = 0; index < 9; ++index)
        {
            labels[index].setBounds(8, 110 + int(index) * 34, content.getWidth() / 2, 26);
            fields[index].setBounds(content.getWidth() / 2 + 4, 110 + int(index) * 34,
                                    content.getWidth() / 2 - 20, 26);
        }
        error.setBounds(16, getHeight() - 78, getWidth() - 32, 30);
        cancel.setBounds(getWidth() - 104, getHeight() - 40, 88, 28);
        reset.setBounds(getWidth() - 200, getHeight() - 40, 88, 28);
        apply.setBounds(getWidth() - 296, getHeight() - 40, 88, 28);
    }
    void paint(juce::Graphics &graphics) override
    {
        graphics.fillAll(juce::Colour(0xff22292f));
    }
};
class PianoNotePropertiesWindow final : public juce::DocumentWindow
{
  public:
    PianoNotePropertiesPanel *panel = nullptr;
    std::function<void()> cancel;
    PianoNotePropertiesWindow() : DocumentWindow("Note Properties", juce::Colour(0xff22292f), closeButton)
    {
        setUsingNativeTitleBar(false);
        setResizable(true, true);
        setResizeLimits(380, 300, 1000, 1000);
        panel = new PianoNotePropertiesPanel;
        setContentOwned(panel, true);
        centreWithSize(490, 590);
    }
    void closeButtonPressed() override
    {
        if (cancel)
            cancel();
    }
    bool keyPressed(const juce::KeyPress &key) override
    {
        if (key == juce::KeyPress::escapeKey)
        {
            closeButtonPressed();
            return true;
        }
        return DocumentWindow::keyPressed(key);
    }
};
} // namespace vibera
