// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Studio.h"
#include "MacroSliceInput.h"
#include "AiPage.h"
#include "MacroStretchInput.h"
#include "SamplerModulationMacroInput.h"
#include "StepShift.h"
#include "StepRelease.h"
#include "StepExpression.h"
#include "NoteRepeat.h"
#include "MixerMacroInput.h"
#include "MixerInsertOrder.h"
#include "MixerMacroContracts.h"
#include "PluginPage.h"
#include "MidiPage.h"
#include "ControllerPage.h"
#include "GuidePage.h"
#include "RackPage.h"
namespace vibera
{
class CaptureKey final : public juce::TextButton
{
  public:
    juce::KeyPress captured;
    CaptureKey() : TextButton("Click, then press a shortcut")
    {
        setWantsKeyboardFocus(true);
        onClick = [this]
        {
            setButtonText("Press a shortcut...");
            grabKeyboardFocus();
        };
    }
    bool keyPressed(const juce::KeyPress &key) override
    {
        captured = key;
        setButtonText(key.getTextDescription());
        return true;
    }
};
class ShortcutPage final : public juce::Component, private juce::ListBoxModel, private juce::Timer
{
    juce::Component::SafePointer<Studio> owner;
    juce::ListBox list{"Actions", this};
    juce::ComboBox scope;
    CaptureKey capture;
    juce::TextButton apply{"Apply"}, alternate{"Add alternate"}, remove{"Remove"},
        reset{"Restore familiar defaults"}, exportProfile{"Export profile"}, importProfile{"Import profile"};
    juce::Label message;
    juce::TextEditor search;
    std::vector<juce::String> actions, names;
    std::unique_ptr<juce::FileChooser> chooser;
    int getNumRows() override
    {
        return int(actions.size());
    }
    void paintListBoxItem(int row, juce::Graphics &g, int width, int h, bool chosen) override
    {
        if (!owner || row < 0 || row >= int(actions.size()))
            return;
        g.fillAll(chosen ? themeColour(0xff304052) : themeColour(0xff1d2937));
        juce::String bindings;
        const juce::StringArray scopes{"Global", "Channel Rack", "Piano roll", "Playlist",
                                       "Mixer",  "Browser",      "Patterns",   "Sampler"};
        for (auto &b : owner->customization.bindings)
            if (b.action == actions[size_t(row)])
                bindings += b.key.getTextDescription() + " (" + scopes[int(b.scope)] + ")  ";
        g.setColour(ink);
        g.setFont(13.f);
        g.drawText(names[size_t(row)] + "    " + bindings, 8, 0, width - 16, h,
                   juce::Justification::centredLeft);
    }
    void selectedRowsChanged(int row) override
    {
        if (row >= 0 && row < int(actions.size()))
            for (auto &c : commandRegistry())
                if (c.id == actions[size_t(row)])
                {
                    scope.setSelectedId(int(c.scope) + 1);
                    break;
                }
    }
    void timerCallback() override
    {
        if (!owner)
            return;
        std::vector<juce::String> nextActions, nextNames;
        auto filter = search.getText();
        for (auto &c : commandRegistry())
            if (c.userAction && (filter.isEmpty() || c.name.containsIgnoreCase(filter)))
            {
                nextActions.push_back(c.id);
                nextNames.push_back(c.name);
            }
        for (auto &m : owner->customization.macros)
            if (filter.isEmpty() || m.name.containsIgnoreCase(filter))
            {
                nextActions.push_back(m.id);
                nextNames.push_back(m.name);
            }
        if (nextActions != actions || nextNames != names)
        {
            actions = std::move(nextActions);
            names = std::move(nextNames);
            list.updateContent();
        }
        list.repaint();
    }
    void report(const juce::Result &r)
    {
        message.setText(r.wasOk() ? "Saved" : r.getErrorMessage(), juce::dontSendNotification);
        if (r.wasOk() && owner)
            owner->saveProfile();
        list.updateContent();
        list.repaint();
    }

  public:
    explicit ShortcutPage(Studio &s) : owner(&s)
    {
        for (auto &c : commandRegistry())
        {
            if (!c.userAction)
                continue;
            actions.push_back(c.id);
            names.push_back(c.name);
        }
        for (auto &m : s.customization.macros)
        {
            actions.push_back(m.id);
            names.push_back(m.name);
        }
        list.setRowHeight(30);
        addAndMakeVisible(list);
        juce::StringArray contexts{"Global", "Channel Rack", "Piano roll", "Playlist",
                                   "Mixer",  "Browser",      "Patterns",   "Sampler"};
        for (int i = 0; i < contexts.size(); ++i)
            scope.addItem(contexts[i], i + 1);
        scope.setSelectedId(1);
        for (auto *c :
             std::initializer_list<juce::Component *>{&scope, &capture, &apply, &alternate, &remove, &reset,
                                                      &exportProfile, &importProfile, &message, &search})
            addAndMakeVisible(c);
        search.setTextToShowWhenEmpty("Search actions and macros", muted);
        search.onTextChange = [this] { timerCallback(); };
        startTimerHz(2);
        apply.onClick = [this]
        {
            int row = list.getSelectedRow();
            if (owner && row >= 0 && row < int(actions.size()))
                report(owner->customization.bind(actions[size_t(row)], Scope(scope.getSelectedId() - 1),
                                                 capture.captured));
        };
        alternate.onClick = [this]
        {
            int row = list.getSelectedRow();
            if (owner && row >= 0 && row < int(actions.size()))
                report(owner->customization.bind(actions[size_t(row)], Scope(scope.getSelectedId() - 1),
                                                 capture.captured, true));
        };
        remove.onClick = [this]
        {
            int row = list.getSelectedRow();
            if (owner && row >= 0 && row < int(actions.size()))
            {
                owner->customization.unbind(actions[size_t(row)], Scope(scope.getSelectedId() - 1));
                report(juce::Result::ok());
            }
        };
        reset.onClick = [this]
        {
            if (owner)
            {
                auto macros = owner->customization.macros;
                const bool restore = owner->customization.restoreMuteAfterSolo;
                owner->customization = Customization::familiarDefaults();
                owner->customization.macros = std::move(macros);
                owner->customization.restoreMuteAfterSolo = restore;
                report(juce::Result::ok());
            }
        };
        exportProfile.onClick = [this]
        {
            chooser =
                std::make_unique<juce::FileChooser>("Export shortcuts and macros", juce::File{}, "*.json");
            auto safe = juce::Component::SafePointer<ShortcutPage>(this);
            chooser->launchAsync(juce::FileBrowserComponent::saveMode |
                                     juce::FileBrowserComponent::canSelectFiles |
                                     juce::FileBrowserComponent::warnAboutOverwriting,
                                 [safe](const juce::FileChooser &fc)
                                 {
                                     if (safe && safe->owner && fc.getResult() != juce::File{})
                                         safe->report(safe->owner->customization.save(
                                             fc.getResult().withFileExtension("json")));
                                 });
        };
        importProfile.onClick = [this]
        {
            chooser =
                std::make_unique<juce::FileChooser>("Import shortcuts and macros", juce::File{}, "*.json");
            auto safe = juce::Component::SafePointer<ShortcutPage>(this);
            chooser->launchAsync(juce::FileBrowserComponent::openMode |
                                     juce::FileBrowserComponent::canSelectFiles,
                                 [safe](const juce::FileChooser &fc)
                                 {
                                     if (safe && safe->owner && fc.getResult().existsAsFile())
                                         safe->report(safe->owner->customization.load(fc.getResult()));
                                 });
        };
    }
    void resized() override
    {
        search.setBounds(12, 10, getWidth() - 24, 28);
        list.setBounds(12, 48, getWidth() - 24, getHeight() - 178);
        scope.setBounds(12, getHeight() - 118, 130, 28);
        capture.setBounds(150, getHeight() - 118, 235, 28);
        apply.setBounds(393, getHeight() - 118, 65, 28);
        alternate.setBounds(466, getHeight() - 118, 110, 28);
        remove.setBounds(584, getHeight() - 118, 80, 28);
        reset.setBounds(12, getHeight() - 80, 205, 28);
        exportProfile.setBounds(225, getHeight() - 80, 130, 28);
        importProfile.setBounds(363, getHeight() - 80, 130, 28);
        message.setBounds(12, getHeight() - 42, getWidth() - 24, 30);
    }
};
class MacroPage final : public juce::Component, private juce::ListBoxModel
{
    juce::Component::SafePointer<Studio> owner;
    Macro draft;
    juce::String definitionGeneration;
    juce::TextEditor name, amount;
    juce::ComboBox action, saved;
    juce::ListBox steps{"Macro steps", this};
    juce::Label hint, message;
    juce::TextButton add{"Add step"}, remove{"Remove"}, up{"Up"}, down{"Down"}, save{"Save macro"},
        run{"Run"}, record{"Record note actions"}, useRecording{"Use recording"}, newMacro{"New"},
        duplicateMacro{"Duplicate"}, deleteMacro{"Delete"};
    std::vector<juce::String> choices, savedIds;
    int getNumRows() override
    {
        return int(draft.steps.size());
    }
    void paintListBoxItem(int row, juce::Graphics &g, int width, int h, bool chosen) override
    {
        if (row < 0 || row >= int(draft.steps.size()))
            return;
        g.fillAll(chosen ? themeColour(0xff304052) : themeColour(0xff1d2937));
        g.setColour(ink);
        g.setFont(13);
        auto &step = draft.steps[size_t(row)];
        auto info = std::find_if(commandRegistry().begin(), commandRegistry().end(),
                                 [&](auto &c) { return c.id == step.action; });
        g.drawText(juce::String(row + 1) + ". " +
                       (info != commandRegistry().end() ? info->name : step.action),
                   8, 0, width - 16, h, juce::Justification::centredLeft);
    }
    void refreshChoices()
    {
        saved.clear(juce::dontSendNotification);
        savedIds.clear();
        if (owner)
            for (size_t i = 0; i < owner->customization.macros.size(); ++i)
            {
                saved.addItem(owner->customization.macros[i].name, int(i) + 1);
                savedIds.push_back(owner->customization.macros[i].id);
            }
    }
    void report(const juce::Result &r)
    {
        message.setText(r.wasOk() ? "Ready" : r.getErrorMessage(), juce::dontSendNotification);
        steps.updateContent();
        steps.repaint();
    }
    void updateHint()
    {
        amount.setEnabled(true);
        amount.setInputRestrictions(12,"0123456789.-");
        int i = action.getSelectedId() - 1;
        if (i < 0 || i >= int(choices.size()))
            return;
        auto id = choices[size_t(i)];
        juce::String description = "Amount";
        double value = 1;
        if (id.startsWith("channel.sampler.modulation."))
        {
            const auto *parameter = samplerModulationParameter(
                id.fromFirstOccurrenceOf("channel.sampler.modulation.", false, false).toStdString());
            amount.setInputRestrictions(64,
                                        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.:-");
            amount.setText("cutoff:" + juce::String(parameter ? parameter->initial : 0),
                           juce::dontSendNotification);
            hint.setText("Destination:value. volume/pan/cutoff/resonance/pitch; Pan LFO only. Flags0/1; "
                         "times seconds or beats.",
                         juce::dontSendNotification);
            return;
        }
        if (isMixerGroupCommand(id))
        {
            const bool text = id == "mixer.group.create" || id.endsWith("rename");
            const bool colour = id.endsWith("colour");
            const bool enabled = id.endsWith("mute") || id.endsWith("solo") || id.endsWith("soloConnected");
            amount.setEnabled(text || colour || enabled);
            amount.setInputRestrictions(text ? 64 : colour ? 10 : 1, text ? "" : "0123456789");
            amount.setText(text ? "Drums" : colour ? "0" : enabled ? "1" : "", juce::dontSendNotification);
            hint.setText("Current Mixer selection/group; name, opaque ARGB/theme0 or enable0/1. Create updates following group steps.", juce::dontSendNotification);
            return;
        }
        if (isMixerInsertOrderCommand(id))
        {
            amount.setEnabled(id == "mixer.move");
            amount.setInputRestrictions(3, "0123456789");
            amount.setText(id == "mixer.move" ? "1" : "", juce::dontSendNotification);
            hint.setText(id == "mixer.move" ? "Final zero-based display row; Master row is reserved" :
                "Current stable Mixer insert moves one non-Master row; boundary is no-op", juce::dontSendNotification);
            return;
        }
        if (id == "mixer.rename")
        {
            amount.setInputRestrictions(64);
            amount.setEnabled(true);
            amount.setText("Drum bus", juce::dontSendNotification);
            hint.setText("Name for current Mixer insert", juce::dontSendNotification);
            return;
        }
        if (id == "mixer.colour")
        {
            amount.setInputRestrictions(10, "0123456789");
            amount.setEnabled(true);
            amount.setText("0", juce::dontSendNotification);
            hint.setText("Opaque ARGB integer (4278190080..4294967295);0=theme", juce::dontSendNotification);
            return;
        }
        if (id == "mixer.unmuteAll")
        {
            amount.setEnabled(false);
            hint.setText("Unmute every Mixer track and clear solo restoration; no arguments",
                         juce::dontSendNotification);
            return;
        }
        if (id == "mixer.mute" || id == "mixer.solo" || id == "mixer.soloConnected" || id == "mixer.unsolo")
        {
            amount.setEnabled(id != "mixer.unsolo");
            amount.setText("1", juce::dontSendNotification);
            hint.setText("Current Mixer insert:1=enable,0=disable; Unsolo restores saved mask",
                         juce::dontSendNotification);
            return;
        }
        if (id == "notes.move")
        {
            description = "Transpose semitones";
            value = 12;
        }
        else if (id == "notes.duplicate" || id == "clip.duplicate")
        {
            description = "Offset in beats";
            value = 4;
        }
        else if (id == "notes.quantize")
        {
            description = "Grid in beats";
            value = .25;
        }
        else if(id=="mixer.route" || id=="mixer.routeOnly" || id=="mixer.send")
        {
            amount.setInputRestrictions(32,"0123456789.:");
            hint.setText(id=="mixer.send"?"Destination:percent (e.g.2:50)":id=="mixer.route"?"Destination:enable (e.g.2:1)":"Destination index (0=Master)",juce::dontSendNotification);
            amount.setText(id=="mixer.send"?"2:50":id=="mixer.route"?"2:1":"2",juce::dontSendNotification);return;
        }
        else if (id == "notes.stretch")
        {
            description = "Time/duration ratio (2=double; 0.5=half)";
            value = 2;
        }
        else if (id == "notes.slice")
        {
            description = "Vertical cut at beat (no snap)";
            value = 1;
        }
        else if (id == "steps.modX" || id == "steps.modY")
        {
            const auto *info =
                stepExpressionInfo(id == "steps.modX" ? StepExpression::modX : StepExpression::modY);
            description =
                id == "steps.modX"
                    ? "Local sampler Mod X cutoff: normalized0-1 / neutral0.5; Filter Off bypasses"
                    : "Local sampler Mod Y resonance: normalized0-1 / neutral0.5; Filter Off bypasses";
            value = info->defaultValue;
        }
        else if (id == "steps.release")
        {
            description = "Release velocity: normalized MIDI note-off (0-1; not ADSR seconds)";
            value = stepReleaseInfo()->defaultValue;
        }
        else if (id == "steps.repeat")
        {
            description = "Repeat rate index (0 Off;10 1/32;13 1/64;14 1/64 triplet)";
            value = 0;
        }
        else if (id == "steps.shift")
        {
            description = "Shift percent (0-100;100 delays0.2475 beats)";
            value = 0;
        }
        else if (id == "notes.velocity")
        {
            description = "Velocity (0-1)";
            value = .8;
        }
        else if (id == "pattern.length")
        {
            description = "Length in beats";
            value = 8;
        }
        else if (id == "project.tempo")
        {
            description = "Tempo (BPM)";
            value = 120;
        }
        else if (id == "channel.sampler.normalize")
        {
            description = "Enabled (0 = off, 1 = on)";
            value = 1;
        }
        else if (id == "channel.mixer")
        {
            description = "Mixer insert";
            value = 1;
        }
        else if (id == "notes.add")
        {
            description = "Pitch (MIDI number)";
            value = 60;
        }
        hint.setText(description + " - targets current selection / Channel", juce::dontSendNotification);
        amount.setEnabled(id != "clip.group" && id != "clip.ungroup" && id != "notes.mute" &&
                          id != "notes.unmute");
        if (!amount.isEnabled())
            hint.setText(id.startsWith("notes.")
                             ? "Selected notes; empty selection uses current Channel. No amount required."
                             : "Selected Playlist Clips; follows grouping mode. No amount required.",
                         juce::dontSendNotification);
        if (id == "notes.slice")
            hint.setText("Current Channel part; cut at this beat without snapping.",
                         juce::dontSendNotification);
        amount.setText(juce::String(value));
    }

  public:
    juce::Result verifyMixerInsertOrderBuilder()
    {
        for (const auto* id : {"mixer.move", "mixer.moveLeft", "mixer.moveRight"})
        {
            const auto found = std::find(choices.begin(), choices.end(), juce::String(id));
            if (found == choices.end()) return juce::Result::fail("Mixer order missing from actual macro builder");
            action.setSelectedId(int(found-choices.begin())+1, juce::sendNotificationSync);
            const auto before = draft.steps.size();
            if (juce::String(id) == "mixer.move")
            {
                if (!amount.isEnabled()) return juce::Result::fail("Absolute order requires typed display row");
                for (const auto* bad : {"", "1.5", "-1", "129", "3junk"})
                {
                    amount.setInputRestrictions(12); amount.setText(bad); add.onClick();
                    if (draft.steps.size() != before) return juce::Result::fail("Invalid macro order row was silently accepted");
                }
                amount.setText("3"); add.onClick();
                if (draft.steps.size() != before+1 || draft.steps.back().parameters.mixerPosition != 3)
                    return juce::Result::fail("Actual macro builder loses typed order row");
            }
            else
            {
                if (amount.isEnabled()) return juce::Result::fail("Relative order must not invent an amount");
                add.onClick();
                if (draft.steps.size() != before+1 || draft.steps.back().parameters.mixerPosition)
                    return juce::Result::fail("Relative order stores no absolute row");
            }
        }
        return juce::Result::ok();
    }
    juce::Result verifyMixerControlBuilder()
    {
        auto choose = [&](const juce::String &name)
        {
            auto i = std::find(choices.begin(), choices.end(), name);
            if (i == choices.end())
                return false;
            action.setSelectedId(int(i - choices.begin()) + 1, juce::sendNotificationSync);
            return true;
        };
        if (!choose("mixer.rename"))
            return juce::Result::fail("Rename missing from macro builder");
        amount.setText("Drum bus");
        add.onClick();
        if (draft.steps.empty() || draft.steps.back().parameters.text != "Drum bus")
            return juce::Result::fail("Name macro field");
        for (const auto *name : {"mixer.pan", "mixer.colour", "mixer.mute"})
        {
            if (!choose(name))
                return juce::Result::fail("Mixer control missing from macro builder");
            const auto count = draft.steps.size();
            amount.setText("invalid");
            add.onClick();
            if (draft.steps.size() != count)
                return juce::Result::fail("Malformed Mixer macro accepted");
            amount.setText(juce::String(name) == "mixer.pan" ? "-0.5" : "1");
            if (juce::String(name) == "mixer.colour")
                amount.setText("4281515426");
            add.onClick();
            if (draft.steps.size() != count + 1)
                return juce::Result::fail("Functional Mixer macro field");
        }
        return juce::Result::ok();
    }
    juce::Result verifySliceBuilder()
    {
        const auto index = std::find(choices.begin(), choices.end(), juce::String("notes.slice"));
        if (index == choices.end())
            return juce::Result::fail("Slice missing from macro builder");
        action.setSelectedId(int(index - choices.begin()) + 1, juce::sendNotificationSync);
        const auto before = draft.steps.size();
        amount.setText("1junk");
        add.onClick();
        if (draft.steps.size() != before)
            return juce::Result::fail("Macro builder accepted malformed Slice beat");
        amount.setText(".5");
        add.onClick();
        if (draft.steps.size() != before + 1 || !draft.steps.back().parameters.slice ||
            draft.steps.back().parameters.slice->startBeat != .5 ||
            draft.steps.back().parameters.slice->endBeat != .5 ||
            draft.steps.back().parameters.slice->grid != 0)
            return juce::Result::fail("Macro builder did not create an exact unsnapped Slice line");
        return juce::Result::ok();
    }
    juce::Result verifyMixerBuilder()
    {
        for(auto id:{"mixer.route","mixer.routeOnly","mixer.send"})
        {
            auto index=std::find(choices.begin(),choices.end(),juce::String(id));
            if(index==choices.end())return juce::Result::fail("Mixer routing missing from macro builder");
            action.setSelectedId(int(index-choices.begin())+1,juce::sendNotificationSync);
            const auto before=draft.steps.size();
            amount.setText("2:bad");add.onClick();
            if(draft.steps.size()!=before)return juce::Result::fail("Mixer macro accepted malformed input");
            amount.setText(juce::String(id)=="mixer.routeOnly"?"2":juce::String(id)=="mixer.route"?"2:1":"2:50");add.onClick();
            if(draft.steps.size()!=before+1 || draft.steps.back().parameters.target!=2 ||
                (juce::String(id)=="mixer.send" && draft.steps.back().parameters.value!=.5))
                return juce::Result::fail("Mixer macro target/level lost");
        }
        return juce::Result::ok();
    }
    juce::Result verifyStretchBuilder()
    {
        const auto index = std::find(choices.begin(), choices.end(), juce::String("notes.stretch"));
        if (index == choices.end()) return juce::Result::fail("Stretch missing from macro builder");
        action.setSelectedId(int(index-choices.begin())+1,juce::sendNotificationSync);
        const auto before=draft.steps.size();
        for(auto invalid:{"1junk","0","-1"})
        {
            amount.setText(invalid);add.onClick();
            if(draft.steps.size()!=before) return juce::Result::fail("Macro builder accepted invalid stretch ratio");
        }
        amount.setText("2");add.onClick();
        if(draft.steps.size()!=before+1 || draft.steps.back().parameters.value!=2)
            return juce::Result::fail("Macro builder lost stretch ratio");
        return juce::Result::ok();
    }
    juce::Result verifyStepShiftBuilder()
    {
        const auto index = std::find(choices.begin(), choices.end(), juce::String("steps.shift"));
        if (index == choices.end())
            return juce::Result::fail("Shift missing from actual macro builder");
        action.setSelectedId(int(index - choices.begin()) + 1, juce::sendNotificationSync);
        if (amount.getText() != "0" || !hint.getText().contains("percent"))
            return juce::Result::fail("Shift macro builder units/default");
        const auto before = draft.steps.size();
        for (const auto *invalid : {"", "-", "-1", "101", "1junk"})
        {
            amount.setInputRestrictions(12);
            amount.setText(invalid);
            add.onClick();
            if (draft.steps.size() != before)
                return juce::Result::fail("Shift macro builder accepted malformed/out-of-range value");
        }
        amount.setText("37.5");
        add.onClick();
        if (draft.steps.size() != before + 1 || draft.steps.back().action != "steps.shift" ||
            draft.steps.back().parameters.value != 37.5 || !draft.steps.back().useCurrentContext)
            return juce::Result::fail("Shift macro builder lost its typed current-Channel value");
        return juce::Result::ok();
    }
    juce::Result verifyStepModulationBuilder()
    {
        for (const auto *id : {"steps.modX", "steps.modY"})
        {
            const auto index = std::find(choices.begin(), choices.end(), juce::String(id));
            if (index == choices.end())
                return juce::Result::fail("Mod X/Y missing from actual macro builder");
            action.setSelectedId(int(index - choices.begin()) + 1, juce::sendNotificationSync);
            if (amount.getText() != "0.5" || !hint.getText().contains("Local sampler") ||
                !hint.getText().contains("normalized0-1") || !hint.getText().contains("neutral0.5"))
                return juce::Result::fail("Mod X/Y macro builder units/neutral/default");
            const auto before = draft.steps.size();
            for (const auto *invalid : {"", "-0.1", "1.1", "nan", "inf", "0.5junk"})
            {
                amount.setInputRestrictions(12);
                amount.setText(invalid);
                add.onClick();
                if (draft.steps.size() != before)
                    return juce::Result::fail(
                        "Mod X/Y macro builder accepted malformed/nonfinite/out-of-range value");
            }
            for (const auto *valid : {"0", ".5", "1"})
            {
                const auto size = draft.steps.size();
                amount.setText(valid);
                add.onClick();
                const double expected = valid[0] == '0' ? 0. : valid[0] == '.' ? .5 : 1.;
                if (draft.steps.size() != size + 1 || draft.steps.back().action != id ||
                    draft.steps.back().parameters.value != expected ||
                    !draft.steps.back().useCurrentContext || draft.steps.back().parameters.pattern != 0 ||
                    draft.steps.back().parameters.channel != 0)
                    return juce::Result::fail("Mod X/Y macro builder lost typed normalized current context");
            }
        }
        return juce::Result::ok();
    }
    juce::Result verifyStepReleaseBuilder()
    {
        const auto index = std::find(choices.begin(), choices.end(), juce::String("steps.release"));
        if (index == choices.end())
            return juce::Result::fail("Release velocity missing from actual macro builder");
        action.setSelectedId(int(index - choices.begin()) + 1, juce::sendNotificationSync);
        if (amount.getText() != "0" || !hint.getText().contains("note-off") ||
            !hint.getText().contains("0-1") || !hint.getText().contains("not ADSR"))
            return juce::Result::fail("Release macro builder note-off units/default");
        const auto before = draft.steps.size();
        for (const auto *invalid : {"", "-0.1", "1.1", "nan", "0.5junk"})
        {
            // Exercise the parser even when programmatic/loaded text bypasses
            // the normal numeric TextEditor character filter.
            amount.setInputRestrictions(12);
            amount.setText(invalid);
            add.onClick();
            if (draft.steps.size() != before)
                return juce::Result::fail(
                    "Release macro builder accepted incomplete/nonfinite/out-of-range input");
        }
        amount.setText(".5");
        add.onClick();
        if (draft.steps.size() != before + 1 || draft.steps.back().action != "steps.release" ||
            draft.steps.back().parameters.value != .5 || !draft.steps.back().useCurrentContext ||
            draft.steps.back().parameters.pattern != 0 || draft.steps.back().parameters.channel != 0)
            return juce::Result::fail("Release macro builder lost normalized current-context note-off data");
        return verifyStepModulationBuilder();
    }
    juce::Result verifyStepRepeatBuilder()
    {
        const auto index = std::find(choices.begin(), choices.end(), juce::String("steps.repeat"));
        if (index == choices.end())
            return juce::Result::fail("Repeat missing from actual macro builder");
        action.setSelectedId(int(index - choices.begin()) + 1, juce::sendNotificationSync);
        if (amount.getText() != "0" || !hint.getText().contains("rate index"))
            return juce::Result::fail("Repeat macro builder units/default");
        const auto before = draft.steps.size();
        for (const auto *invalid : {"", "-1", "15", "2.5", "1junk"})
        {
            amount.setInputRestrictions(12);
            amount.setText(invalid);
            add.onClick();
            if (draft.steps.size() != before)
                return juce::Result::fail("Repeat macro builder accepted malformed/fractional rate");
        }
        amount.setText("13");
        add.onClick();
        if (draft.steps.size() != before + 1 || draft.steps.back().action != "steps.repeat" ||
            draft.steps.back().parameters.value != 13 || !draft.steps.back().useCurrentContext)
            return juce::Result::fail("Repeat macro builder lost its typed rate index");
        return juce::Result::ok();
    }
    explicit MacroPage(Studio &s) : owner(&s)
    {
        definitionGeneration = s.macroConfigurationGeneration();
        draft.id = "macro." + juce::Uuid().toString();
        name.setText("My variation");
        amount.setInputRestrictions(12, "0123456789.-");
        for (auto &c : commandRegistry())
            if ((c.userAction || c.id == "steps.velocity" || c.id == "steps.pitch" || c.id == "steps.pan" ||
                 c.id == "steps.fine" || c.id == "steps.shift" || c.id == "steps.repeat" ||
                 c.id == "steps.release" || c.id == "steps.modX" || c.id == "steps.modY") &&
                c.documentEdit && c.id != "channel.paste" && c.id != "channel.add" &&
                c.id != "channel.source")
            {
                choices.push_back(c.id);
                action.addItem(c.name, int(choices.size()));
            }
        for (auto *c : std::initializer_list<juce::Component *>{
                 &name, &amount, &action, &saved, &steps, &hint, &message, &add, &remove, &up, &down, &save,
                 &run, &record, &useRecording, &newMacro, &duplicateMacro, &deleteMacro})
            addAndMakeVisible(c);
        steps.setRowHeight(30);
        action.onChange = [this] { updateHint(); };
        action.setSelectedId(1, juce::dontSendNotification);
        updateHint();
        refreshChoices();
        saved.onChange = [this]
        {
            int i = saved.getSelectedId() - 1;
            if (owner && i >= 0 && i < int(savedIds.size()))
            {
                const auto target = std::find_if(owner->customization.macros.begin(),owner->customization.macros.end(),
                    [&](const auto &entry) { return entry.id == savedIds[size_t(i)]; });
                if (target == owner->customization.macros.end())
                {
                    report(juce::Result::fail("macro.unavailable: Saved recipe was removed; refresh/select its stable ID again"));
                    return;
                }
                draft = *target;
                definitionGeneration = owner->macroConfigurationGeneration();
                name.setText(draft.name);
                report(juce::Result::ok());
            }
        };
        add.onClick = [this]
        {
            int i = action.getSelectedId() - 1;
            if (i < 0 || i >= int(choices.size()))
                return;
            auto id = choices[size_t(i)];
            CommandArgs a;
            a.pitch = 0;
            a.length = 0;
            double value = amount.getText().getDoubleValue();
            if (id.startsWith("channel.sampler.modulation."))
            {
                if (auto result = parseSamplerModulationMacro(id, amount.getText(), a); result.failed())
                {
                    report(result);
                    return;
                }
            }
            else if (isMixerGroupCommand(id))
            {
                if (id == "mixer.group.create" || id.endsWith("rename")) a.text = amount.getText().trim();
                else if (id.endsWith("colour"))
                {
                    const auto input = amount.getText().trim().toStdString();
                    const auto parsed = std::from_chars(input.data(), input.data()+input.size(), a.value);
                    if (parsed.ec != std::errc{} || parsed.ptr != input.data()+input.size() || !validMixerColour(a.value))
                    { report(juce::Result::fail("mixer.group.invalidColour: Enter theme0 or opaque ARGB")); return; }
                }
                else if (id == "mixer.group.mute" || id == "mixer.group.solo" || id == "mixer.group.soloConnected")
                {
                    if (amount.getText() != "0" && amount.getText() != "1")
                    { report(juce::Result::fail("mixer.group.invalidArguments: Enable0 or1")); return; }
                    a.enabled = amount.getText() == "1";
                }
            }
            else if (id == "mixer.move")
            {
                const auto input = amount.getText().trim().toStdString();
                int position = -1;
                const auto parsed = std::from_chars(input.data(), input.data()+input.size(), position);
                if (parsed.ec != std::errc{} || parsed.ptr != input.data()+input.size() || position < 0 || position > 128)
                { report(juce::Result::fail("mixer.invalidPosition: Enter an exact display row0..128")); return; }
                a.mixerPosition = position;
            }
            else if (id == "mixer.moveLeft" || id == "mixer.moveRight")
            { /* Relative canonical alias has no amount argument. */ }
            else if (id == "mixer.rename")
                a.text = amount.getText();
            else if (id == "mixer.mute" || id == "mixer.solo" || id == "mixer.soloConnected")
            {
                if (amount.getText() != "0" && amount.getText() != "1")
                {
                    report(juce::Result::fail("Mixer enable requires0 or1"));
                    return;
                }
                a.enabled = amount.getText() == "1";
            }
            else if (id == "mixer.pan" || id == "mixer.gain" || id == "mixer.colour")
            {
                auto input = amount.getText().trim().toStdString();
                auto parsed = std::from_chars(input.data(), input.data() + input.size(), a.value);
                if (parsed.ec != std::errc{} || parsed.ptr != input.data() + input.size() ||
                    !std::isfinite(a.value))
                {
                    report(juce::Result::fail("Enter a complete finite Mixer value"));
                    return;
                }
            }
            else if (id == "steps.modX" || id == "steps.modY")
            {
                const auto input = amount.getText().trim().toStdString();
                const auto parsed = std::from_chars(input.data(), input.data() + input.size(), a.value);
                const auto *info =
                    stepExpressionInfo(id == "steps.modX" ? StepExpression::modX : StepExpression::modY);
                if (parsed.ec != std::errc{} || parsed.ptr != input.data() + input.size() ||
                    !std::isfinite(a.value) || a.value < info->minimum || a.value > info->maximum)
                {
                    report(juce::Result::fail("Mod X/Y requires a complete finite normalized value0..1"));
                    return;
                }
            }
            else if (id == "steps.release")
            {
                const auto input = amount.getText().trim().toStdString();
                const auto parsed = std::from_chars(input.data(), input.data() + input.size(), a.value);
                const auto *info = stepReleaseInfo();
                if (parsed.ec != std::errc{} || parsed.ptr != input.data() + input.size() ||
                    !std::isfinite(a.value) || a.value < info->minimum || a.value > info->maximum)
                {
                    report(juce::Result::fail(
                        "Release velocity requires a complete finite normalized note-off value0..1"));
                    return;
                }
            }
            else if (id == "steps.repeat")
            {
                const auto input = amount.getText().trim().toStdString();
                int mode = -1;
                const auto parsed = std::from_chars(input.data(), input.data() + input.size(), mode);
                if (parsed.ec != std::errc{} || parsed.ptr != input.data() + input.size() ||
                    !noteRepeatRate(mode))
                {
                    report(juce::Result::fail("Repeat requires a complete rhythmic rate index0..14"));
                    return;
                }
                a.value = mode;
            }
            else if (id == "steps.shift")
            {
                const auto input = amount.getText().trim().toStdString();
                const auto parsed = std::from_chars(input.data(), input.data() + input.size(), a.value);
                const auto *info = stepShiftInfo();
                if (parsed.ec != std::errc{} || parsed.ptr != input.data() + input.size() ||
                    !std::isfinite(a.value) || a.value < info->minimum || a.value > info->maximum)
                {
                    report(juce::Result::fail("Shift requires a complete finite percent0..100"));
                    return;
                }
            }
            else if (id == "notes.move")
                a.pitch = int(value);
            else if (id == "notes.duplicate" || id == "clip.duplicate" || id == "steps.set")
                a.beat = value;
            else if(id=="mixer.route" || id=="mixer.routeOnly" || id=="mixer.send")
            {
                if(auto result=parseMixerMacro(id,amount.getText(),a);result.failed()){report(result);return;}
            }
            else if (id == "notes.stretch")
            {
                if (auto result = parseMacroStretchRatio(amount.getText(), a.value); result.failed())
                {
                    report(result); return;
                }
            }
            else if (id == "notes.slice")
            {
                NoteSliceLine line;
                if (auto result = parseMacroSliceBeat(amount.getText(), line); result.failed())
                {
                    report(result);
                    return;
                }
                a.slice = line;
            }
            else if (id == "notes.quantize" || id == "pattern.length")
                a.length = value;
            else if (id == "channel.sampler.normalize")
            {
                const auto setting = amount.getText().trim();
                if (setting != "0" && setting != "1")
                {
                    report(juce::Result::fail("Normalize requires0 (off) or1 (on)"));
                    return;
                }
                a.enabled = setting == "1";
            }
            else if (id == "channel.mixer")
                a.mixer = int(value);
            else if (id == "notes.add")
            {
                a.pitch = int(value);
                a.length = .5;
                a.value = .8;
            }
            else if (id == "clip.add")
            {
                a.beat = value;
                a.length = 4;
            }
            else
                a.value = value;
            draft.steps.push_back({id, a, true});
            report(juce::Result::ok());
        };
        remove.onClick = [this]
        {
            int i = steps.getSelectedRow();
            if (i >= 0 && i < int(draft.steps.size()))
                draft.steps.erase(draft.steps.begin() + i);
            report(juce::Result::ok());
        };
        up.onClick = [this]
        {
            int i = steps.getSelectedRow();
            if (i > 0 && i < int(draft.steps.size()))
            {
                std::swap(draft.steps[size_t(i)], draft.steps[size_t(i - 1)]);
                steps.selectRow(i - 1);
            }
            report(juce::Result::ok());
        };
        down.onClick = [this]
        {
            int i = steps.getSelectedRow();
            if (i >= 0 && i + 1 < int(draft.steps.size()))
            {
                std::swap(draft.steps[size_t(i)], draft.steps[size_t(i + 1)]);
                steps.selectRow(i + 1);
            }
            report(juce::Result::ok());
        };
        save.onClick = [this]
        {
            if (!owner)
                return;
            draft.name = name.getText();
            auto r = owner->changeMacro(MacroConfigurationChange::upsert,draft,{},definitionGeneration);
            if (r.wasOk())
            {
                definitionGeneration = owner->macroConfigurationGeneration(); refreshChoices();
            }
            report(r);
        };
        run.onClick = [this]
        {
            if (!owner)
                return;
            draft.name = name.getText();
            auto r = owner->changeMacro(MacroConfigurationChange::upsert,draft,{},definitionGeneration);
            if (r.wasOk())
            {
                definitionGeneration = owner->macroConfigurationGeneration();
                auto run = owner->runSavedMixerMacro(draft.id,definitionGeneration);
                message.setText(run.wasOk() ? owner->status : run.getErrorMessage(),juce::dontSendNotification);
            }
            else
                report(r);
        };
        record.setButtonText(s.recordingNotes ? "Stop recording" : "Record note actions");
        record.onClick = [this]
        {
            if (owner)
            {
                owner->recordingNotes = !owner->recordingNotes;
                record.setButtonText(owner->recordingNotes ? "Stop recording" : "Record note actions");
                if (owner->recordingNotes)
                {
                    owner->recordedSteps.clear();
                    owner->status = "Recording note actions - return to Settings to finish";
                    owner->closeSettings();
                }
            }
        };
        useRecording.onClick = [this]
        {
            if (owner)
            {
                owner->recordingNotes = false;
                record.setButtonText("Record note actions");
                draft.steps = owner->recordedSteps;
                report(juce::Result::ok());
            }
        };
        newMacro.onClick = [this]
        {
            draft = {"macro." + juce::Uuid().toString(), "New macro", {}};
            definitionGeneration = owner ? owner->macroConfigurationGeneration() : juce::String{};
            name.setText(draft.name);
            saved.setSelectedId(0, juce::dontSendNotification);
            report(juce::Result::ok());
        };
        duplicateMacro.onClick = [this]
        {
            draft.id = "macro." + juce::Uuid().toString();
            definitionGeneration = owner ? owner->macroConfigurationGeneration() : juce::String{};
            draft.name = name.getText() + " copy";
            name.setText(draft.name);
            saved.setSelectedId(0, juce::dontSendNotification);
            report(juce::Result::ok());
        };
        deleteMacro.onClick = [this]
        {
            if (owner)
            {
                auto r = owner->changeMacro(MacroConfigurationChange::remove,{},draft.id,definitionGeneration);
                if (r.wasOk())
                {
                    definitionGeneration = owner->macroConfigurationGeneration();
                    draft = {"macro." + juce::Uuid().toString(), "New macro", {}};
                    name.setText(draft.name);
                    refreshChoices();
                }
                report(r);
            }
        };
    }
    void resized() override
    {
        name.setBounds(12, 12, 180, 28);
        saved.setBounds(202, 12, 260, 28);
        newMacro.setBounds(472, 12, 70, 28);
        duplicateMacro.setBounds(550, 12, 100, 28);
        deleteMacro.setBounds(658, 12, 80, 28);
        action.setBounds(12, 52, 270, 28);
        amount.setBounds(292, 52, 100, 28);
        add.setBounds(402, 52, 100, 28);
        hint.setBounds(12, 86, getWidth() - 24, 26);
        steps.setBounds(12, 118, getWidth() - 24, getHeight() - 242);
        remove.setBounds(12, getHeight() - 112, 90, 28);
        up.setBounds(110, getHeight() - 112, 60, 28);
        down.setBounds(178, getHeight() - 112, 60, 28);
        save.setBounds(246, getHeight() - 112, 110, 28);
        run.setBounds(364, getHeight() - 112, 70, 28);
        record.setBounds(12, getHeight() - 74, 180, 28);
        useRecording.setBounds(200, getHeight() - 74, 150, 28);
        message.setBounds(12, getHeight() - 36, getWidth() - 24, 28);
    }
};
class ColourPage final : public juce::Component, private juce::ListBoxModel, private juce::ChangeListener
{
    Studio &studio;
    juce::ListBox roles{"Appearance colours", this};
    juce::ColourSelector picker{juce::ColourSelector::showColourAtTop | juce::ColourSelector::showSliders |
                                juce::ColourSelector::showColourspace};
    juce::TextButton save{"Save appearance"}, reset{"Restore defaults"};
    juce::Label message, fontLabel{{}, "Font"}, presetLabel{{}, "Theme"};
    juce::ComboBox font, preset;
    juce::StringArray fonts;
    int selectedRole = 0;
    void showFont()
    {
        font.setSelectedItemIndex(std::max(0, fonts.indexOf(uiFont, true)), juce::dontSendNotification);
    }
    bool appearanceQueued = false;
    int getNumRows() override
    {
        return int(themeRoles.size());
    }
    void paintListBoxItem(int row, juce::Graphics &g, int width, int height, bool chosen) override
    {
        if (row < 0 || row >= getNumRows())
            return;
        g.fillAll(chosen ? grid : panel);
        g.setColour(*themeRoles[size_t(row)].colour);
        g.fillRect(8, 8, 18, height - 16);
        label(g, themeRoles[size_t(row)].name, {34, 0, width - 38, height}, ink, 12);
    }
    void selectedRowsChanged(int row) override
    {
        if (row < 0 || row >= getNumRows())
            return;
        selectedRole = row;
        picker.setCurrentColour(*themeRoles[size_t(row)].colour, juce::dontSendNotification);
    }
    void changeListenerCallback(juce::ChangeBroadcaster *) override
    {
        *themeRoles[size_t(selectedRole)].colour = picker.getCurrentColour();
        preset.setSelectedId(0, juce::dontSendNotification); // A hand-edited palette is custom.
        // Changing LookAndFeel while ColourSelector is updating its RGB sliders
        // re-enters those sliders and can overwrite the remaining components.
        if (!appearanceQueued)
        {
            appearanceQueued = true;
            juce::MessageManager::callAsync(
                [safe = juce::Component::SafePointer<ColourPage>(this)]
                {
                    if (!safe)
                        return;
                    safe->appearanceQueued = false;
                    safe->studio.applyAppearance();
                });
        }
        roles.repaint();
        repaint();
        message.setText("Live preview. Save to retain after restart.", juce::dontSendNotification);
    }

  public:
    explicit ColourPage(Studio &s) : studio(s)
    {
        for (auto *control : std::initializer_list<juce::Component *>{&roles, &picker, &save, &reset, &message,
                                                                      &fontLabel, &font, &presetLabel, &preset})
            addAndMakeVisible(control);
        preset.addItem("VibeRa (default)", 1);
        for (size_t i = 0; i < themePresets.size(); ++i)
            preset.addItem(themePresets[i].name, int(i) + 2);
        preset.setTextWhenNothingSelected("Custom");
        preset.setTooltip("Replace every colour with a built-in palette (High contrast for low vision)");
        preset.onChange = [this]
        {
            const int chosen = preset.getSelectedId();
            if (chosen <= 0)
                return;
            if (chosen == 1)
            {
                const auto keptFont = uiFont; // The palette does not change the font.
                resetTheme();
                uiFont = keptFont;
            }
            else
                applyThemePreset(themePresets[size_t(chosen - 2)]);
            selectedRowsChanged(selectedRole);
            studio.applyAppearance();
            roles.repaint();
            repaint();
            message.setText("Live preview. Save to retain after restart.", juce::dontSendNotification);
        };
        // Installed fonts, the default first; choosing one previews it across the interface.
        fonts = juce::Font::findAllTypefaceNames();
        fonts.removeString(defaultUiFont, true);
        fonts.insert(0, defaultUiFont);
        for (int i = 0; i < fonts.size(); ++i)
            font.addItem(fonts[i] + (i == 0 ? " (default)" : ""), i + 1);
        showFont();
        font.setTooltip("Interface font for every editor, panel and dialog");
        font.onChange = [this]
        {
            if (font.getSelectedItemIndex() < 0)
                return;
            uiFont = fonts[font.getSelectedItemIndex()];
            studio.applyAppearance();
            message.setText("Live preview. Save to retain after restart.", juce::dontSendNotification);
        };
        roles.setRowHeight(32);
        roles.selectRow(0);
        picker.addChangeListener(this);
        save.onClick = [this]
        {
            auto result = saveTheme(themeFile());
            message.setText(result.wasOk() ? "Appearance saved" : result.getErrorMessage(),
                            juce::dontSendNotification);
        };
        reset.onClick = [this]
        {
            resetTheme();
            showFont();
            selectedRowsChanged(selectedRole);
            studio.applyAppearance();
            roles.repaint();
            message.setText("Default preview restored. Save to retain.", juce::dontSendNotification);
        };
    }
    ~ColourPage() override
    {
        picker.removeChangeListener(this);
    }
    juce::Result verifyForTest(const juce::File &folder)
    {
        std::array<juce::Colour, 24> previous;
        for (size_t i = 0; i < previous.size(); ++i)
            previous[i] = *themeRoles[i].colour;
        const auto musicalState = studio.controller.project.state();
        selectedRowsChanged(0);
        // Match normal GUI notifications; synchronous setters re-enter this JUCE revision's RGB sliders.
        picker.setCurrentColour(juce::Colour(0xff273544), juce::sendNotification);
        picker.dispatchPendingMessages();
        const bool live = bg == juce::Colour(0xff273544);
        auto saved = saveTheme(folder.getChildFile("gui-appearance.json"));
        reset.onClick();
        const bool resetWorked = bg == juce::Colour(themeRoles[0].initial);
        auto restored = loadTheme(folder.getChildFile("gui-appearance.json"));
        const bool reloadWorked = bg == juce::Colour(0xff273544);
        // Font: choose another installed font, save, reset to the default and reload it.
        const auto previousFont = uiFont;
        bool fontWorked = true;
        if (fonts.size() > 1)
        {
            const auto otherIndex = fonts.indexOf("Arial", true) > 0 ? fonts.indexOf("Arial", true) : 1;
            const auto other = fonts[otherIndex];
            font.setSelectedItemIndex(otherIndex, juce::sendNotificationSync);
            const bool chosen = uiFont == other;
            if (auto *top = studio.getTopLevelComponent())
            {
                top->repaint();
                juce::PNGImageFormat png;
                juce::FileOutputStream shot(folder.getChildFile("appearance-font.png"));
                if (shot.openedOk())
                {
                    shot.setPosition(0);
                    shot.truncate();
                    png.writeImageToStream(top->createComponentSnapshot(top->getLocalBounds()), shot);
                }
            }
            const auto fontFile = folder.getChildFile("gui-appearance-font.json");
            const bool savedFont = saveTheme(fontFile).wasOk();
            reset.onClick();
            const bool resetFont = uiFont == defaultUiFont && font.getSelectedItemIndex() == 0;
            const bool reloadedFont = loadTheme(fontFile).wasOk() && uiFont == other;
            auto profile = juce::JSON::parse(fontFile);
            profile.getDynamicObject()->setProperty("font", "No Such Font VibeRa");
            fontFile.replaceWithText(juce::JSON::toString(profile));
            const bool missingFallsBack = loadTheme(fontFile).wasOk() && uiFont == defaultUiFont;
            fontWorked = chosen && savedFont && resetFont && reloadedFont && missingFallsBack;
        }
        // High contrast preset: black workspace, white text; captured for inspection.
        preset.setSelectedId(2, juce::sendNotificationSync);
        const bool contrast = bg == juce::Colour(0xff000000) && ink == juce::Colour(0xffffffff) &&
                              grid == juce::Colour(0xff505050) && uiFont == defaultUiFont;
        if (auto *top = studio.getTopLevelComponent())
        {
            juce::PNGImageFormat png;
            juce::FileOutputStream shot(folder.getChildFile("appearance-high-contrast.png"));
            if (shot.openedOk())
            {
                shot.setPosition(0);
                shot.truncate();
                png.writeImageToStream(top->createComponentSnapshot(top->getLocalBounds()), shot);
            }
        }
        preset.setSelectedId(1, juce::sendNotificationSync);
        const bool defaults = bg == juce::Colour(themeRoles[0].initial);
        if (!contrast || !defaults)
            fontWorked = false;
        uiFont = previousFont;
        showFont();
        for (size_t i = 0; i < previous.size(); ++i)
            *themeRoles[i].colour = previous[i];
        studio.applyAppearance();
        selectedRowsChanged(selectedRole);
        if (!fontWorked)
            return juce::Result::fail("Appearance font choice/save/reset/reload/missing fallback or theme presets");
        return live && saved.wasOk() && resetWorked && restored.wasOk() && reloadWorked &&
                       musicalState.isEquivalentTo(studio.controller.project.state())
                   ? juce::Result::ok()
                   : juce::Result::fail("Colour picker/reset/profile or musical-state preservation");
    }
    void paint(juce::Graphics &g) override
    {
        g.fillAll(panel);
    }
    void resized() override
    {
        roles.setBounds(12, 12, 250, getHeight() - 100);
        presetLabel.setBounds(12, getHeight() - 80, 50, 28);
        preset.setBounds(64, getHeight() - 80, 198, 28);
        picker.setBounds(274, 12, getWidth() - 286, getHeight() - 104);
        save.setBounds(274, getHeight() - 80, 160, 28);
        reset.setBounds(442, getHeight() - 80, 160, 28);
        fontLabel.setBounds(614, getHeight() - 80, 40, 28);
        font.setBounds(656, getHeight() - 80, std::max(140, getWidth() - 668), 28);
        message.setBounds(12, getHeight() - 40, getWidth() - 24, 28);
    }
};
// Keep every settings control reachable when the tab is smaller than its useful layout.
class ScrollingSettingsPage final : public juce::Component
{
    juce::Viewport viewport;
    int minimumWidth, minimumHeight;

  public:
    explicit ScrollingSettingsPage(juce::Component *page, int width = 740, int height = 500)
        : minimumWidth(std::max(width, page->getWidth())),
          minimumHeight(page->getHeight() > 0 ? page->getHeight() : height) // A page's own height is its content.
    {
        addAndMakeVisible(viewport);
        viewport.setScrollBarsShown(true, true);
        viewport.setViewedComponent(page, true);
    }
    juce::Component *page() const
    {
        return viewport.getViewedComponent();
    }
    // The size that shows the whole page without scrolling (its own size when it set one).
    // The bottom of the page's visible controls counts, so pages that reserve spare height
    // (the device selector) do not open with empty space.
    juce::Point<int> naturalSize() const
    {
        int bottom = 0;
        if (auto *page = viewport.getViewedComponent())
            for (auto *child : page->getChildren())
                if (child->isVisible())
                    bottom = std::max(bottom, child->getBottom());
        return {minimumWidth, bottom > 0 ? std::min(minimumHeight, bottom + 16) : minimumHeight};
    }
    void resized() override
    {
        viewport.setBounds(getLocalBounds());
        if (auto *page = viewport.getViewedComponent())
            page->setSize(std::max(minimumWidth, getWidth() - scrollBarThickness),
                          std::max(minimumHeight, getHeight() - scrollBarThickness));
    }
};
class SettingsContent final : public juce::TabbedComponent
{
  public:
    PluginPage *pluginPage() const
    {
        auto *holder = dynamic_cast<ScrollingSettingsPage *>(getTabContentComponent(3));
        return holder ? dynamic_cast<PluginPage *>(holder->page()) : nullptr;
    }
    explicit SettingsContent(Studio &studio) : TabbedComponent(juce::TabbedButtonBar::TabsAtTop)
    {
        setLookAndFeel(&studio.getLookAndFeel());
        auto colour = panel;
        addTab(
            "Audio", colour,
            new ScrollingSettingsPage(new juce::AudioDeviceSelectorComponent(
                studio.audio.engine.getDeviceManager().deviceManager, 0, 2, 2, 2, false, false, true, true)),
            true);
        addTab("Shortcuts", colour, new ScrollingSettingsPage(new ShortcutPage(studio)), true);
        addTab("Macros", colour, new ScrollingSettingsPage(new MacroPage(studio)), true);
        addTab("Plugins", colour, new ScrollingSettingsPage(new PluginPage(studio)), true);
        addTab("MIDI", colour, new ScrollingSettingsPage(new MidiPage(studio)), true);
        addTab("Transport", colour, new ScrollingSettingsPage(new GuidePage(studio)), true);
        addTab("Workflow", colour, new ScrollingSettingsPage(new RackPage(studio)), true);
        addTab("Colours", colour, new ScrollingSettingsPage(new ColourPage(studio)), true);
        addTab("Controllers", colour, new ScrollingSettingsPage(new ControllerPage(studio)), true);
        addTab("AI", colour, new ScrollingSettingsPage(new AiPage(studio)), true);
        setSize(760, 560);
        fitToCurrentTab();
    }
    // Opens (and switches tabs) at the size that shows the whole page, within the screen.
    void currentTabChanged(int, const juce::String &) override
    {
        fitToCurrentTab();
    }
    void fitToCurrentTab()
    {
        auto *holder = dynamic_cast<ScrollingSettingsPage *>(getCurrentContentComponent());
        if (!holder)
            return;
        const auto natural = holder->naturalSize();
        auto area = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->userArea;
        if (auto *peer = getPeer())
            if (const auto *display = juce::Desktop::getInstance().getDisplays().getDisplayForRect(peer->getBounds()))
                area = display->userArea;
        const int width = std::min(std::max(760, natural.x + 4), area.getWidth() - 40);
        const int height = std::min(natural.y + getTabBarDepth() + 4, area.getHeight() - 80);
        setSize(width, std::max(240, height));
        if (auto *window = getTopLevelComponent(); window != this)
            window->setBounds(window->getBounds().constrainedWithin(area));
    }
};
} // namespace vibera
