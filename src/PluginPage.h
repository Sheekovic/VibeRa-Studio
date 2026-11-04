// SPDX-License-Identifier: AGPL-3.0-or-later
#include "synth/SynthRegistry.h"
#include "NativeInstruments.h"
#pragma once
#include "Studio.h"
#include "effects/NativeEffectsRegistry.h"
#include <algorithm>
#include <utility>
namespace vibera
{
class PluginPage final : public juce::Component, private juce::ListBoxModel, private juce::Timer
{
    juce::Component::SafePointer<Studio> owner;
    juce::TextEditor folders, report;
    juce::ListBox browser{"VibeRa instruments, effects and VST3 plugins", this};
    juce::ComboBox channel, mixer;
    juce::TextButton scan{"Scan VST3"}, retry{"Retry blacklist"}, cancel{"Cancel scan"},
        create{"New Channel"}, replace{"Replace Channel"}, insert{"Insert effect"},
        channelSlots{"Channel plugin..."}, effectSlots{"Mixer slots..."};
    juce::Label title, progress;
    juce::ToggleButton standardLocations{"VST3 standard locations"};
    juce::String observedOperation, localError;
    std::vector<juce::PluginDescription> types;
    std::vector<Id> channelIds;
    juce::StringArray channelNames;
    bool wasScanning = false;
    juce::String mixerProject;
    std::vector<std::pair<int, juce::String>> mixerTargets;
    void refreshMixerTargets()
    {
        if (!owner)
            return;
        std::vector<std::pair<int, juce::String>> targets;
        for (const auto &entry : owner->controller.project.mixer)
            targets.emplace_back(entry.index, entry.name);
        const bool sameProject = mixerProject == owner->controller.projectIdentity;
        if (sameProject && targets == mixerTargets)
            return;
        int selected = sameProject ? mixer.getSelectedId() - 1 : owner->selectedInsert;
        if (std::none_of(targets.begin(), targets.end(),
                         [selected](const auto &entry) { return entry.first == selected; }))
            selected = targets.empty() ? -1 : targets.front().first;
        mixer.clear(juce::dontSendNotification);
        for (const auto &[index, name] : targets)
            mixer.addItem(name, index + 1);
        mixer.setSelectedId(selected + 1, juce::dontSendNotification);
        mixerProject = owner->controller.projectIdentity;
        mixerTargets = std::move(targets);
    }
    Id selectedChannelId() const
    {
        auto index = channel.getSelectedItemIndex();
        return index >= 0 && size_t(index) < channelIds.size() ? channelIds[size_t(index)] : 0;
    }
    int getNumRows() override
    {
        return int(types.size());
    }
    void paintListBoxItem(int row, juce::Graphics &g, int w, int h, bool selected) override
    {
        if (row < 0 || row >= int(types.size()))
            return;
        auto &type = types[size_t(row)];
        g.fillAll(selected ? grid : panel);
        const auto category = isNativeInstrument(type) ? juce::String("VibeRa instrument  ") : effects::isNativeDescription(type)
                                  ? juce::String("VibeRa effect  ")
                                  : juce::String(type.isInstrument ? "VST3 instrument  " : "VST3 effect  ");
        label(g, category + type.name, {8, 0, w - 16, h}, ink, 13);
    }
    void selectedRowsChanged(int) override
    {
        updateButtons();
    }
    void updateButtons()
    {
        auto row = browser.getSelectedRow();
        bool valid = row >= 0 && row < int(types.size());
        create.setEnabled(valid && types[size_t(row)].isInstrument);
        replace.setEnabled(valid && types[size_t(row)].isInstrument && channel.getSelectedId() > 0);
        insert.setEnabled(valid && !types[size_t(row)].isInstrument);
    }
    void add(const juce::String &action)
    {
        int row = browser.getSelectedRow();
        if (!owner || row < 0 || row >= int(types.size()))
            return;
        if (action == "plugin.effect.insert" &&
            (mixerProject != owner->controller.projectIdentity || mixer.getSelectedId() <= 0 ||
             std::none_of(owner->controller.project.mixer.begin(), owner->controller.project.mixer.end(),
                          [&](const auto &entry) { return entry.index == mixer.getSelectedId() - 1; })))
        {
            localError = "Mixer target changed; reopen Add effect for the current project";
            return;
        }
        CommandArgs a;
        a.text = types[size_t(row)].createXml()->toString();
        a.channel = selectedChannelId();
        a.mixer = mixer.getSelectedId() - 1;
        owner->command(action, a);
        if (action == "plugin.instrument.create")
            owner->selectedChannel = owner->controller.project.channels.back().id;
        owner->refresh();
    }
    void timerCallback() override
    {
        if (!owner)
            return;
        refreshMixerTargets();
        auto snapshot = owner->plugins.snapshot();
        const bool newOperation = snapshot.operation != observedOperation;
        if (newOperation)
        {
            observedOperation = snapshot.operation;
            folders.setText(snapshot.requestedFolders.joinIntoString("\n"), false);
            standardLocations.setToggleState(snapshot.requestedStandardLocations, juce::dontSendNotification);
        }
        // Built-in instruments/effects are local picker entries. Scanning and its persisted
        // catalogue remain exclusively about external VST3 plugins.
        auto nextTypes = effects::nativeDescriptions();
        const auto instruments = nativeInstrumentDescriptions();
        nextTypes.insert(nextTypes.begin(), instruments.begin(), instruments.end());
        const auto nativeCount = nextTypes.size();
        nextTypes.insert(nextTypes.end(), snapshot.plugins.begin(), snapshot.plugins.end());
        const bool changedTypes =
            nextTypes.size() != types.size() ||
            !std::equal(nextTypes.begin(), nextTypes.end(), types.begin(), [](const auto &a, const auto &b)
                        { return a.createXml()->toString() == b.createXml()->toString(); });
        if (newOperation || changedTypes || (wasScanning && !snapshot.scanning))
        {
            const int row = browser.getSelectedRow();
            const auto selected = row >= 0 && row < int(types.size())
                                      ? types[size_t(row)].createIdentifierString()
                                      : juce::String{};
            types = std::move(nextTypes);
            browser.updateContent();
            browser.deselectAllRows();
            for (size_t i = 0; i < types.size(); ++i)
                if (types[i].createIdentifierString() == selected)
                {
                    browser.selectRow(int(i));
                    break;
                }
        }
        wasScanning = snapshot.scanning;
        scan.setEnabled(!snapshot.scanning);
        retry.setEnabled(!snapshot.scanning);
        cancel.setEnabled(snapshot.completionState == "pending" && !snapshot.cancelRequested);
        folders.setEnabled(!snapshot.scanning);
        standardLocations.setEnabled(!snapshot.scanning);
        progress.setText("VibeRa: " + juce::String(int(nativeCount)) +
                             " native instruments/effects   |   VST3: " + snapshot.progress + "   " +
                             juce::String(snapshot.completed) + " / " + juce::String(snapshot.total),
                         juce::dontSendNotification);
        auto text = "VST3 blacklisted: " + juce::String(snapshot.blacklist.size()) + ". " +
                    snapshot.blacklist.joinIntoString("\n") + "\n" + snapshot.failures.joinIntoString("\n") +
                    "\n" + snapshot.error + "\n" + localError;
        report.setText(text, false);
        std::vector<Id> ids;
        juce::StringArray names;
        for (auto &c : owner->controller.project.channels)
        {
            ids.push_back(c.id);
            names.add(c.name);
        }
        if (ids != channelIds || names != channelNames)
        {
            auto selected = selectedChannelId();
            if (!selected)
                selected = owner->selectedChannel;
            channelIds = std::move(ids);
            channelNames = std::move(names);
            channel.clear(juce::dontSendNotification);
            for (size_t i = 0; i < channelIds.size(); ++i)
            {
                channel.addItem(channelNames[int(i)], int(i) + 1);
                if (channelIds[i] == selected)
                    channel.setSelectedId(int(i) + 1, juce::dontSendNotification);
            }
        }
        updateButtons();
    }

  public:
    juce::Result verifyDiscoveryControls(const juce::File &output, const juce::File &fixture);
    juce::Result selectMixerInsert(int index, const juce::String &project)
    {
        if (!owner || owner->controller.projectIdentity != project)
            return juce::Result::fail("Project changed; reopen Add effect");
        if (std::none_of(owner->controller.project.mixer.begin(), owner->controller.project.mixer.end(),
                         [index](const auto &entry) { return entry.index == index; }))
            return juce::Result::fail("Mixer insert is unavailable");
        refreshMixerTargets();
        mixer.setSelectedId(index + 1, juce::dontSendNotification);
        updateButtons();
        return juce::Result::ok();
    }
    int selectedMixerInsert() const
    {
        return mixer.getSelectedId() - 1;
    }
    const juce::String &targetProjectIdentity() const
    {
        return mixerProject;
    }
    explicit PluginPage(Studio &studio) : owner(&studio)
    {
        for (auto *c : std::initializer_list<juce::Component *>{
                 &folders, &report, &browser, &channel, &mixer, &scan, &retry, &create, &replace, &insert,
                 &channelSlots, &effectSlots, &title, &progress, &cancel, &standardLocations})
            addAndMakeVisible(c);
        title.setText("VibeRa instruments and effects included  |  Windows x64 VST3 plugins",
                      juce::dontSendNotification);
        folders.setMultiLine(true);
        const auto initial = studio.plugins.snapshot();
        const bool pending = initial.completionState == "pending";
        observedOperation = initial.operation;
        wasScanning = initial.scanning;
        folders.setText((pending ? initial.requestedFolders : initial.folders).joinIntoString("\n"));
        standardLocations.setToggleState(pending ? initial.requestedStandardLocations
                                                 : initial.standardLocations,
                                         juce::dontSendNotification);
        standardLocations.setTooltip("Include the Windows VST3 standard folders. Enabled by default; disable "
                                     "for a targeted custom-folder scan.");
        scan.setTooltip(
            "Discover external VST3 plugins. VibeRa instruments and effects are available without a scan.");
        retry.setTooltip("Retry blacklisted external VST3 plugins in the selected scan folders.");
        folders.setTextToShowWhenEmpty("Custom VST3 folders, one absolute path per line", muted);
        report.setMultiLine(true);
        report.setReadOnly(true);
        browser.setRowHeight(30);
        refreshMixerTargets();
        scan.onClick = [this]
        {
            if (owner)
            {
                auto r = owner->plugins.scan(juce::StringArray::fromLines(folders.getText()), false,
                                             standardLocations.getToggleState());
                localError = r.getErrorMessage();
            }
        };
        retry.onClick = [this]
        {
            if (owner)
                localError = owner->plugins
                                 .scan(juce::StringArray::fromLines(folders.getText()), true,
                                       standardLocations.getToggleState())
                                 .getErrorMessage();
        };
        cancel.onClick = [this]
        {
            if (owner)
                localError = owner->plugins.cancel(owner->plugins.snapshot().operation).getErrorMessage();
        };
        create.onClick = [this] { add("plugin.instrument.create"); };
        replace.onClick = [this] { add("plugin.instrument.replace"); };
        insert.onClick = [this] { add("plugin.effect.insert"); };
        channel.onChange = [this]
        {
            if (owner && channel.getSelectedId())
                owner->selectedChannel = selectedChannelId();
            updateButtons();
        };
        channelSlots.onClick = [this]
        {
            if (owner)
                owner->pluginMenu(selectedChannelId());
        };
        effectSlots.onClick = [this]
        {
            if (owner)
                owner->effectMenu(mixer.getSelectedId() - 1);
        };
        timerCallback();
        startTimer(200);
    }
    void resized() override
    {
        auto r = getLocalBounds().reduced(12);
        title.setBounds(r.removeFromTop(24));
        auto options = r.removeFromTop(28);
        standardLocations.setBounds(options.removeFromLeft(210));
        cancel.setBounds(options.removeFromRight(124));
        auto scanRow = r.removeFromTop(56);
        scan.setBounds(scanRow.removeFromRight(124).reduced(0, 10));
        retry.setBounds(scanRow.removeFromRight(124).reduced(8, 10));
        folders.setBounds(scanRow.reduced(0, 3));
        progress.setBounds(r.removeFromTop(28));
        report.setBounds(r.removeFromBottom(66));
        auto controls = r.removeFromRight(224).reduced(10, 0);
        channel.setBounds(controls.removeFromTop(28));
        controls.removeFromTop(8);
        create.setBounds(controls.removeFromTop(28));
        controls.removeFromTop(8);
        replace.setBounds(controls.removeFromTop(28));
        controls.removeFromTop(8);
        channelSlots.setBounds(controls.removeFromTop(28));
        controls.removeFromTop(22);
        mixer.setBounds(controls.removeFromTop(28));
        controls.removeFromTop(8);
        insert.setBounds(controls.removeFromTop(28));
        controls.removeFromTop(8);
        effectSlots.setBounds(controls.removeFromTop(28));
        browser.setBounds(r.reduced(0, 4));
    }
};
} // namespace vibera
