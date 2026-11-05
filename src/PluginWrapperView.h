// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginWrapperProtocol.h"
#include "PluginBuses.h"
#include <functional>
#include <optional>
#include <utility>
#include <vector>

namespace vibera
{
// Logical coordinates; retain vendor size while bringing host-page controls
// onto their current monitor. This does not scale an oversized native editor.
inline juce::Rectangle<int> pluginWrapperBoundsWithinWorkArea(juce::Rectangle<int> bounds,
                                                              juce::Rectangle<int> workArea)
{
    if (workArea.isEmpty())
        return bounds;
    return bounds.withPosition(
        juce::jlimit(workArea.getX(), juce::jmax(workArea.getX(), workArea.getRight() - bounds.getWidth()),
                     bounds.getX()),
        juce::jlimit(workArea.getY(), juce::jmax(workArea.getY(), workArea.getBottom() - bounds.getHeight()),
                     bounds.getY()));
}
struct PluginWrapperParameterView
{
    std::uint32_t id = 0, generation = 0;
    juce::String name, label;
    float value = 0, defaultValue = 0;
    bool writable = false, hasDefault = false;
    bool automatable = false;
};

// GUI-thread view only. The native editor is borrowed and must outlive this
// component. Admission is not completion: all authoritative values arrive in
// update(), and all mutations go through the supplied application proxy.
class PluginWrapperViewComponent final : public juce::Component, private juce::ComponentListener
{
  public:
    PluginWrapperViewComponent(juce::Component *nativeEditor,
                               std::function<bool(const PluginWrapperIntent &)> submit,
                               juce::ComponentBoundsConstrainer *nativeBounds = nullptr,
                               bool hostResizable = false)
        : native(nativeEditor), send(std::move(submit)), nativeConstrainer(nativeBounds),
          resizeNative(hostResizable)
    {
        for (auto *button : {&pluginTab, &settingsTab, &parametersTab})
        {
            addAndMakeVisible(button);
            button->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff30363d));
            button->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff376c68));
        }
        pluginTab.setEnabled(native != nullptr);
        pluginTab.setTooltip(native ? "Show native plugin editor"
                                    : "No native editor is available; use Settings or Parameters");
        pluginTab.onClick = [this]
        {
            if (native)
                setView(PluginWrapperView::plugin);
        };
        settingsTab.onClick = [this] { setView(PluginWrapperView::settings); };
        parametersTab.onClick = [this] { setView(PluginWrapperView::parameters); };
        for (auto *label :
             {&status, &feedbackLabel, &settingsHeading, &mixLabel, &parameterLabel, &unavailable, &ioHeading,
              &inputBusLabel, &outputBusLabel, &inputMappingLabel, &ioHint, &chainHeading, &chainHint})
        {
            addAndMakeVisible(label);
            label->setColour(juce::Label::textColourId, juce::Colour(0xffd6dde3));
        }
        // Section headings read as headings; hints and status are secondary text.
        for (auto *heading : {&settingsHeading, &ioHeading, &chainHeading})
        {
            heading->setFont(juce::Font("Segoe UI", 13.5f, juce::Font::bold));
            heading->setColour(juce::Label::textColourId, juce::Colour(0xff29d7c5));
        }
        for (auto *secondary : {&status, &feedbackLabel, &ioHint, &chainHint})
        {
            secondary->setFont(juce::Font("Segoe UI", 12.f, juce::Font::plain));
            secondary->setColour(juce::Label::textColourId, juce::Colour(0xff95a9b4));
        }
        settingsHeading.setText("Host processing", juce::dontSendNotification);
        mixLabel.setText("Effect wet / dry", juce::dontSendNotification);
        unavailable.setText("No native editor is available. Use Parameters for exposed controls.",
                            juce::dontSendNotification);
        unavailable.setJustificationType(juce::Justification::centred);
        addAndMakeVisible(bypass);
        bypass.setTooltip("Host bypass; waits for the application's actual result");
        bypass.onClick = [this]
        {
            bypass.setToggleState(state.bypassed, juce::dontSendNotification);
            if (independentReady())
                admit({PluginWrapperAction::bypass, 0, 0, state.bypassed ? 0.f : 1.f, false});
        };
        configureSlider(mix);
        mix.setRange(0, 1, .0001);
        mix.setName("Effect wet/dry");
        mix.setTitle("Effect wet/dry");
        // Internal checkpoint: one canonical request on release. Continuous live
        // host mix gestures are not qualified by this release-only interaction.
        mix.setChangeNotificationOnlyOnRelease(true);
        mix.setTextValueSuffix(" wet");
        mix.onValueChange = [this]
        {
            if (independentReady() && !state.instrument)
                admit({PluginWrapperAction::mix, 0, 0, float(mix.getValue()), false});
        };
        for (auto *knob : {&channelPan, &channelVol, &channelPitch})
        {
            addChildComponent(*knob);
            knob->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
            knob->setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
            knob->setRange(0, 1, .001);
            knob->setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(0xff70c2b5));
            knob->setChangeNotificationOnlyOnRelease(true);
            knob->setDoubleClickReturnValue(true, knob == &channelVol ? .78 / 1.25 : .5);
        }
        channelPan.setTooltip("Channel pan (double-click: centre)");
        channelVol.setTooltip("Channel volume (double-click: default)");
        channelPan.onValueChange = [this]
        {
            if (independentReady() && state.channel.present)
                admit({PluginWrapperAction::channelPan, 0, 0, float(channelPan.getValue()), false});
        };
        channelPitch.setTooltip("Channel pitch: pitch bend sent with every note (double-click: centre)");
        channelPitch.onValueChange = [this]
        {
            if (independentReady() && state.channel.present)
                admit({PluginWrapperAction::channelPitch, 0, 0, float(channelPitch.getValue()), false});
        };
        addChildComponent(rangeButton);
        rangeButton.setTooltip("Pitch RANGE in semitones (set the plugin's bend range to match)");
        rangeButton.onClick = [this]
        {
            juce::PopupMenu menu;
            for (int semitones : {1, 2, 3, 5, 7, 12, 24, 48})
                menu.addItem(semitones, juce::String(semitones) + " semitones", true, state.channel.range == semitones);
            menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&rangeButton),
                               [safe = juce::Component::SafePointer<PluginWrapperViewComponent>(this)](int chosen)
                               {
                                   if (safe && chosen > 0 && safe->independentReady() && safe->state.channel.present)
                                       safe->admit({PluginWrapperAction::channelRange, std::uint32_t(chosen), 0, 0.f, false});
                               });
        };
        channelVol.onValueChange = [this]
        {
            if (independentReady() && state.channel.present)
                admit({PluginWrapperAction::channelGain, 0, 0, float(channelVol.getValue()), false});
        };
        addChildComponent(channelOn);
        channelOn.setTooltip("Channel on / muted");
        channelOn.onClick = [this]
        {
            const bool wanted = channelOn.getToggleState();
            channelOn.setToggleState(state.channel.on, juce::dontSendNotification);
            if (independentReady() && state.channel.present)
                admit({PluginWrapperAction::channelOn, 0, 0, wanted ? 1.f : 0.f, false});
        };
        for (auto *button : {&trackDown, &trackUp})
        {
            addChildComponent(*button);
            button->onClick = [this, up = button == &trackUp]
            {
                if (!independentReady() || !state.channel.present)
                    return;
                const int next = juce::jlimit(0, state.channel.maxTrack, state.channel.track + (up ? 1 : -1));
                if (next != state.channel.track)
                    admit({PluginWrapperAction::channelTrack, std::uint32_t(next), 0, 0.f, false});
            };
        }
        trackDown.setTooltip("Mixer track: previous insert");
        trackUp.setTooltip("Mixer track: next insert");
        addChildComponent(trackLabel);
        trackLabel.setJustificationType(juce::Justification::centred);
        trackLabel.setColour(juce::Label::backgroundColourId, juce::Colour(0xff8a3d45));
        trackLabel.setColour(juce::Label::textColourId, juce::Colours::white);
        trackLabel.setTooltip("Mixer TRACK this instrument plays into (0 = Master)");
        addAndMakeVisible(choices);
        choices.setTextWhenNothingSelected("No exposed parameters");
        choices.onChange = [this]
        {
            const auto index = choices.getSelectedId() - 1;
            if (index >= 0 && size_t(index) < parameters.size())
            {
                const auto id = parameters[size_t(index)].id;
                const auto generation = parameters[size_t(index)].generation;
                if (selectParameter(id, generation))
                    return;
            }
            choices.setSelectedId(selected + 1, juce::dontSendNotification);
        };
        configureSlider(value);
        value.setRange(0, 1, .0001);
        value.setName("Parameter value");
        value.setTitle("Parameter value");
        value.onDragStart = [this]
        {
            finishGesture();
            dragging = true;
            closedDrag = false;
            const auto *parameter = currentParameter();
            if (!parameter || !parameter->writable || !state.ready || state.pending || controlPending)
                return;
            gesture = {true, parameter->id, parameter->generation, float(value.getValue())};
            if (!admit({PluginWrapperAction::parameter, gesture.id, gesture.generation, gesture.value, true}))
                gesture.active = false;
        };
        value.onValueChange = [this]
        {
            if (dragging)
            {
                if (!gesture.active)
                    return; // Never reopen a rejected or already closed drag.
                const auto requested = float(value.getValue());
                if (!admit({PluginWrapperAction::parameter, gesture.id, gesture.generation, requested, true}))
                    finishGesture();
                else
                    gesture.value = requested;
                return;
            }
            if (closedDrag && value.isMouseButtonDown())
                return;
            const auto *parameter = currentParameter();
            if (parameter && parameter->writable && state.ready && !state.pending && !controlPending)
                admit({PluginWrapperAction::parameter, parameter->id, parameter->generation,
                       float(value.getValue()), false});
        };
        value.onDragEnd = [this]
        {
            finishGesture();
            dragging = false;
            refreshParameter();
        };
        addAndMakeVisible(reset);
        reset.onClick = [this]
        {
            finishGesture();
            const auto *parameter = currentParameter();
            if (parameter && parameter->writable && parameter->hasDefault && state.ready && !state.pending &&
                !controlPending)
                admit({PluginWrapperAction::parameter, parameter->id, parameter->generation,
                       parameter->defaultValue, false});
        };
        for (auto *button : {&createAutomation, &initialize, &removeInitialization, &refresh, &lastTweaked})
            addAndMakeVisible(button);
        createAutomation.setTooltip("Create an Automation Clip for this exact exposed native parameter");
        initialize.setTooltip("Initialize the Song with this parameter's actual native value");
        removeInitialization.setTooltip("Remove this parameter's saved initialization if present");
        refresh.setTooltip("Request fresh native parameter metadata; wait for the application result");
        lastTweaked.setTooltip("Select the published last-tweaked parameter in this current catalogue");
        createAutomation.onClick = [this] { parameterAction(PluginWrapperAction::automationCreate); };
        initialize.onClick = [this] { parameterAction(PluginWrapperAction::initialize); };
        removeInitialization.onClick = [this] { parameterAction(PluginWrapperAction::removeInitialization); };
        refresh.onClick = [this]
        {
            if (independentReady())
                admit({PluginWrapperAction::refresh, 0, 0, 0, false});
        };
        lastTweaked.onClick = [this]
        {
            if (independentReady())
                if (const auto *parameter = lastTweakedParameter())
                {
                    const auto id = parameter->id, generation = parameter->generation;
                    if (selectParameter(id, generation))
                        setView(PluginWrapperView::parameters);
                }
        };
        addAndMakeVisible(notifications);
        notifications.setTooltip("Native parameter notifications; the checkbox follows acknowledged state");
        notifications.onClick = [this]
        {
            notifications.setToggleState(state.notifications, juce::dontSendNotification);
            if (independentReady())
                admit({PluginWrapperAction::notifications, 0, 0, state.notifications ? 0.f : 1.f, false});
        };
        ioHeading.setText("Native main audio I/O", juce::dontSendNotification);
        inputBusLabel.setText("Input bus", juce::dontSendNotification);
        outputBusLabel.setText("Output bus", juce::dontSendNotification);
        inputMappingLabel.setText("Input mapping", juce::dontSendNotification);
        ioHint.setText("Apply requests structural reconfiguration; this editor may close.",
                       juce::dontSendNotification);
        for (auto *box : {&inputBus, &outputBus, &inputMapping})
            addAndMakeVisible(box);
        inputBus.setName("Native input bus");
        outputBus.setName("Native output bus");
        inputMapping.setName("Native input mapping");
        inputBus.setTextWhenNothingSelected("Native bus unavailable");
        outputBus.setTextWhenNothingSelected("Native bus unavailable");
        inputMapping.addItem("Stereo (L / R)", 1);
        inputMapping.addItem("Mono from left", 2);
        inputMapping.addItem("Mono from right", 3);
        inputMapping.addItem("Mono average (L + R) / 2", 4);
        inputBus.onChange = [this]
        {
            if (independentReady() && routingUsable && routingCatalogue.inputCount > 0)
                draftIo.inputBus = inputBus.getSelectedId() - 1;
            else
                refreshRoutingSelections();
            refreshEnabled();
        };
        outputBus.onChange = [this]
        {
            if (independentReady() && routingUsable)
                draftIo.outputBus = outputBus.getSelectedId() - 1;
            else
                refreshRoutingSelections();
            refreshEnabled();
        };
        inputMapping.onChange = [this]
        {
            if (independentReady() && routingUsable && routingCatalogue.inputCount > 0)
            {
                const int choice = inputMapping.getSelectedId();
                draftIo.inputChannels = choice == 1 ? 2 : choice >= 2 && choice <= 4 ? 1 : 0;
                draftIo.monoSource = choice >= 2 && choice <= 4 ? choice - 1 : 0;
            }
            else
                refreshRoutingSelections();
            refreshEnabled();
        };
        addAndMakeVisible(applyIo);
        applyIo.setTooltip(
            "Prepare the requested native buses/mapping before canonical publication; not seamless");
        applyIo.onClick = [this]
        {
            if (independentReady() && routingDraftValid() && draftIo != authoritativeIo)
            {
                PluginWrapperIntent intent;
                intent.action = PluginWrapperAction::audioIo;
                intent.io = draftIo;
                admit(intent);
            }
        };
        chainHeading.setText("Containing Mixer chain", juce::dontSendNotification);
        for (auto *button :
             {&copyChain, &pasteChain, &saveChain, &loadChain, &chainDirectory, &detectorRouting})
            addAndMakeVisible(button);
        copyChain.setTooltip("Capture and copy the Mixer effect chain containing this effect");
        pasteChain.setTooltip("Apply the copied chain to this effect's containing Mixer insert");
        saveChain.setTooltip("Capture this effect's containing Mixer chain and choose a preset file");
        loadChain.setTooltip("Choose a chain preset for this effect's containing Mixer insert");
        chainDirectory.setTooltip("Choose the containing-chain preset directory through the application");
        detectorRouting.setTooltip(
            "Choose detector routing for this exact Mixer effect through the application");
        copyChain.onClick = [this] { chainAction(PluginWrapperAction::chainCopy); };
        pasteChain.onClick = [this] { chainAction(PluginWrapperAction::chainPaste); };
        saveChain.onClick = [this] { chainAction(PluginWrapperAction::chainSave); };
        loadChain.onClick = [this] { chainAction(PluginWrapperAction::chainLoad); };
        chainDirectory.onClick = [this] { chainAction(PluginWrapperAction::chainDirectory); };
        detectorRouting.onClick = [this] { chainAction(PluginWrapperAction::sidechain); };
        if (native)
        {
            naturalWidth = juce::jmax(1, native->getWidth());
            naturalHeight = juce::jmax(1, native->getHeight());
            addAndMakeVisible(native);
            native->addComponentListener(this);
        }
        applyView();
        resizeForView();
        refreshEnabled();
    }

    ~PluginWrapperViewComponent() override
    {
        finishGesture();
        if (native)
        {
            native->removeComponentListener(this);
            removeChildComponent(native); // Borrowed: never delete the native editor.
        }
    }

    // View selection is local UI state, changed by setView(). The owner may use
    // that method for external requests and publish selectedView() as readback;
    // stale processing-state snapshots must not switch the user's current tab.
    void update(const PluginWrapperState &next, const std::vector<PluginWrapperParameterView> &nextParameters,
                const juce::String &feedback)
    {
        state = next;
        controlPending = next.pending;
        {
            const bool strip = next.channel.present;
            for (juce::Component *c : std::initializer_list<juce::Component *>{&channelOn, &channelPan, &channelVol,
                                                                               &channelPitch, &rangeButton,
                                                                               &trackDown, &trackUp, &trackLabel})
                if (c->isVisible() != strip)
                {
                    c->setVisible(strip);
                    resized();
                }
            channelOn.setToggleState(next.channel.on, juce::dontSendNotification);
            if (!channelPan.isMouseButtonDown())
                channelPan.setValue(next.channel.pan, juce::dontSendNotification);
            if (!channelVol.isMouseButtonDown())
                channelVol.setValue(next.channel.gain, juce::dontSendNotification);
            if (!channelPitch.isMouseButtonDown())
                channelPitch.setValue(next.channel.pitch, juce::dontSendNotification);
            rangeButton.setButtonText(juce::String(next.channel.range) + " st");
            trackLabel.setText("TRACK " + juce::String(next.channel.track), juce::dontSendNotification);
        }
        std::uint32_t selectedId = 0, selectedGeneration = 0;
        const bool hadSelection = currentParameter() != nullptr;
        if (const auto *parameter = currentParameter())
        {
            selectedId = parameter->id;
            selectedGeneration = parameter->generation;
        }
        bool choicesChanged = parameters.size() != nextParameters.size();
        if (!choicesChanged)
            for (size_t i = 0; i < parameters.size(); ++i)
                if (parameters[i].id != nextParameters[i].id ||
                    parameters[i].generation != nextParameters[i].generation ||
                    parameters[i].name != nextParameters[i].name)
                {
                    choicesChanged = true;
                    break;
                }
        parameters = nextParameters;
        selected = parameters.empty() ? -1 : 0;
        bool gestureStillValid = !gesture.active;
        for (size_t i = 0; i < parameters.size(); ++i)
        {
            const auto &parameter = parameters[i];
            if (hadSelection && parameter.id == selectedId && parameter.generation == selectedGeneration)
                selected = int(i);
            if (gesture.active && parameter.id == gesture.id && parameter.generation == gesture.generation &&
                parameter.writable)
                gestureStillValid = true;
        }
        if (!state.ready || !gestureStillValid)
            finishGesture();
        if (choicesChanged)
        {
            choices.clear(juce::dontSendNotification);
            for (size_t i = 0; i < parameters.size(); ++i)
                choices.addItem(parameters[i].name.isNotEmpty()
                                    ? parameters[i].name
                                    : "Parameter " + juce::String(parameters[i].id),
                                int(i) + 1);
        }
        choices.setSelectedId(selected + 1, juce::dontSendNotification);
        bypass.setToggleState(state.bypassed, juce::dontSendNotification);
        notifications.setToggleState(state.notifications, juce::dontSendNotification);
        if (!mix.isMouseButtonDown())
            mix.setValue(state.mix, juce::dontSendNotification);
        feedbackLabel.setText(feedback, juce::dontSendNotification);
        status.setText(!state.ready    ? "Unavailable"
                       : state.pending ? "Pending"
                                       : "Ready",
                       juce::dontSendNotification);
        refreshParameter();
        refreshEnabled();
    }

    // Immutable prepared metadata, not proof that another combined layout will
    // negotiate. Identical readbacks never discard a user's unsubmitted draft.
    void updateRouting(const PluginMainIo &io, const PluginBusCatalogue &catalogue)
    {
        const bool changed = !routingReceived || io != authoritativeIo;
        const bool catalogueChanged = !sameCatalogue(routingCatalogue, catalogue);
        routingReceived = true;
        authoritativeIo = io;
        if (changed)
            draftIo = io;
        routingCatalogue = catalogue;
        routingUsable = io.valid() && catalogue.valid() && io.outputBus < catalogue.outputCount &&
                        (catalogue.inputCount == 0 || io.inputBus < catalogue.inputCount);
        if (catalogueChanged)
        {
            inputBus.clear(juce::dontSendNotification);
            outputBus.clear(juce::dontSendNotification);
            if (catalogue.valid())
                for (int i = 0; i < catalogue.inputCount + catalogue.outputCount; ++i)
                {
                    const auto &row = catalogue.entries[std::size_t(i)];
                    auto caption = juce::String(row.index) + ": " + juce::String::fromUTF8(row.name);
                    if (caption.endsWith(": "))
                        caption += row.input ? "Input" : "Output";
                    caption += " (default " + juce::String(row.defaultChannels) + "ch)";
                    (row.input ? inputBus : outputBus).addItem(caption, row.index + 1);
                }
        }
        inputBus.setTextWhenNothingSelected(catalogue.valid() && catalogue.inputCount == 0
                                                ? "No native input buses"
                                                : "Native bus unavailable");
        refreshRoutingSelections();
        refreshEnabled();
    }

    void setView(PluginWrapperView next)
    {
        if (next != PluginWrapperView::plugin && next != PluginWrapperView::settings &&
            next != PluginWrapperView::parameters)
            return;
        if (view == next)
            return;
        finishGesture();
        view = next;
        applyView();
        resizeForView();
        if (onNativeSizeChanged)
            onNativeSizeChanged();
    }

    PluginWrapperView selectedView() const
    {
        return view;
    }

    bool selectParameter(std::uint32_t id, std::uint32_t generation)
    {
        for (size_t i = 0; i < parameters.size(); ++i)
            if (parameters[i].id == id && parameters[i].generation == generation)
            {
                finishGesture();
                selected = int(i);
                choices.setSelectedId(selected + 1, juce::dontSendNotification);
                refreshParameter();
                return true;
            }
        return false; // Unknown/stale identity changes neither selection nor gesture.
    }

    std::optional<std::pair<std::uint32_t, std::uint32_t>> selectedParameterIdentity() const
    {
        if (const auto *parameter = currentParameter())
            return std::pair{parameter->id, parameter->generation};
        return std::nullopt;
    }

    void finishGesture()
    {
        dragging = false;
        closedDrag = true;
        if (!gesture.active)
            return;
        const auto ending = gesture;
        gesture.active = false; // Reentry cannot close/reopen the same gesture twice.
        admit({PluginWrapperAction::parameter, ending.id, ending.generation, ending.value, false});
    }

    // Preferred wrapper size changed, including after a genuine native-editor
    // resize. The owner resizes its window; host resizing is opt-in and obeys
    // the editor's supported bounds rather than scaling its pixels.
    std::function<void()> onNativeSizeChanged;

    void paint(juce::Graphics &g) override
    {
        g.fillAll(juce::Colour(0xff20252b));
        g.setColour(juce::Colour(0xff343c44));
        g.drawHorizontalLine(toolbarHeight - 1, 0.f, float(getWidth()));
        if (channelPan.isVisible())
        {
            g.setColour(juce::Colour(0xffb8c7cf));
            g.setFont(juce::Font(juce::FontOptions(11.f, juce::Font::bold)));
            for (auto *knob : {&channelPan, &channelVol, &channelPitch})
                g.drawText(knob == &channelPan ? "PAN" : knob == &channelVol ? "VOL" : "PITCH", knob->getRight() + 2,
                           knob->getY(), 36, knob->getHeight(), juce::Justification::centredLeft);
        }
    }

    void resized() override
    {
        auto toolbar = getLocalBounds().removeFromTop(toolbarHeight).reduced(8, 6);
        pluginTab.setBounds(toolbar.removeFromLeft(82));
        toolbar.removeFromLeft(4);
        settingsTab.setBounds(toolbar.removeFromLeft(86));
        toolbar.removeFromLeft(4);
        parametersTab.setBounds(toolbar.removeFromLeft(104));
        if (channelOn.isVisible())
        {
            toolbar.removeFromLeft(10);
            channelOn.setBounds(toolbar.removeFromLeft(50));
            channelPan.setBounds(toolbar.removeFromLeft(32));
            toolbar.removeFromLeft(30); // "PAN" label (painted).
            channelVol.setBounds(toolbar.removeFromLeft(32));
            toolbar.removeFromLeft(30); // "VOL" label (painted).
            channelPitch.setBounds(toolbar.removeFromLeft(32));
            toolbar.removeFromLeft(38); // "PITCH" label (painted).
            rangeButton.setBounds(toolbar.removeFromLeft(48));
            toolbar.removeFromLeft(6);
            toolbar.removeFromLeft(6);
            trackDown.setBounds(toolbar.removeFromLeft(22));
            trackLabel.setBounds(toolbar.removeFromLeft(70));
            trackUp.setBounds(toolbar.removeFromLeft(22));
        }
        bypass.setBounds(toolbar.removeFromRight(84));
        status.setBounds(toolbar);
        feedbackLabel.setBounds(getLocalBounds().removeFromBottom(footerHeight).reduced(8, 2));
        auto body =
            getLocalBounds().withTrimmedTop(toolbarHeight).withTrimmedBottom(footerHeight).reduced(16);
        if (native && resizeNative && !choosingSize && !resizingNative && view == PluginWrapperView::plugin)
        {
            const juce::ScopedValueSetter<bool> guard(resizingNative, true);
            auto bounds = juce::Rectangle<int>(0, toolbarHeight, getWidth(),
                juce::jmax(1, getHeight() - toolbarHeight - footerHeight));
            if (nativeConstrainer)
                nativeConstrainer->setBoundsForComponent(native, bounds, false, false, true, true);
            else
                native->setBounds(bounds);
            naturalWidth = native->getWidth();
            naturalHeight = native->getHeight();
        }
        if (native)
            native->setTopLeftPosition(juce::jmax(0, (getWidth() - naturalWidth) / 2), toolbarHeight);
        unavailable.setBounds(body);
        settingsHeading.setBounds(body.removeFromTop(32));
        body.removeFromTop(16);
        mixLabel.setBounds(body.removeFromTop(24));
        mix.setBounds(body.removeFromTop(40));
        body.removeFromTop(16);
        notifications.setBounds(body.removeFromTop(32));
        body.removeFromTop(12);
        ioHeading.setBounds(body.removeFromTop(20));
        body.removeFromTop(4);
        auto inputRow = body.removeFromTop(30);
        inputBusLabel.setBounds(inputRow.removeFromLeft(108));
        inputBus.setBounds(inputRow);
        body.removeFromTop(6);
        auto outputRow = body.removeFromTop(30);
        outputBusLabel.setBounds(outputRow.removeFromLeft(108));
        outputBus.setBounds(outputRow);
        body.removeFromTop(6);
        auto mappingRow = body.removeFromTop(30);
        inputMappingLabel.setBounds(mappingRow.removeFromLeft(108));
        inputMapping.setBounds(mappingRow);
        body.removeFromTop(10);
        applyIo.setBounds(body.removeFromTop(32));
        body.removeFromTop(4);
        ioHint.setBounds(body.removeFromTop(20));
        body.removeFromTop(12);
        chainHeading.setBounds(body.removeFromTop(24));
        body.removeFromTop(4);
        auto firstChainRow = body.removeFromTop(32);
        const int chainColumn = (firstChainRow.getWidth() - 16) / 3;
        copyChain.setBounds(firstChainRow.removeFromLeft(chainColumn));
        firstChainRow.removeFromLeft(8);
        pasteChain.setBounds(firstChainRow.removeFromLeft(chainColumn));
        firstChainRow.removeFromLeft(8);
        saveChain.setBounds(firstChainRow);
        body.removeFromTop(8);
        auto secondChainRow = body.removeFromTop(32);
        loadChain.setBounds(secondChainRow.removeFromLeft(chainColumn));
        secondChainRow.removeFromLeft(8);
        chainDirectory.setBounds(secondChainRow.removeFromLeft(chainColumn));
        secondChainRow.removeFromLeft(8);
        detectorRouting.setBounds(secondChainRow);
        body.removeFromTop(6);
        chainHint.setBounds(body.removeFromTop(36));
        auto parameterBody =
            getLocalBounds().withTrimmedTop(toolbarHeight).withTrimmedBottom(footerHeight).reduced(16);
        choices.setBounds(parameterBody.removeFromTop(32));
        parameterBody.removeFromTop(12);
        parameterLabel.setBounds(parameterBody.removeFromTop(32));
        auto row = parameterBody.removeFromTop(40);
        reset.setBounds(row.removeFromRight(82));
        row.removeFromRight(10);
        value.setBounds(row);
        parameterBody.removeFromTop(12);
        auto automationRow = parameterBody.removeFromTop(36);
        createAutomation.setBounds(automationRow.removeFromLeft(224));
        automationRow.removeFromLeft(8);
        initialize.setBounds(automationRow.removeFromLeft(110));
        automationRow.removeFromLeft(8);
        removeInitialization.setBounds(automationRow);
        parameterBody.removeFromTop(10);
        auto actions = parameterBody.removeFromTop(32);
        refresh.setBounds(actions.removeFromLeft(200));
        actions.removeFromLeft(8);
        lastTweaked.setBounds(actions.removeFromLeft(140));
    }

    void visibilityChanged() override
    {
        if (!isShowing())
            finishGesture();
    }

  private:
    static constexpr int toolbarHeight = 44, footerHeight = 40;
    juce::Component *native = nullptr;
    std::function<bool(const PluginWrapperIntent &)> send;
    juce::ComponentBoundsConstrainer *nativeConstrainer = nullptr;
    bool resizeNative = false, resizingNative = false, choosingSize = false;
    PluginWrapperState state;
    PluginMainIo authoritativeIo, draftIo;
    PluginBusCatalogue routingCatalogue;
    bool routingReceived = false, routingUsable = false;
    PluginWrapperView view = PluginWrapperView::plugin;
    std::vector<PluginWrapperParameterView> parameters;
    int selected = -1, naturalWidth = 560, naturalHeight = 256;
    bool dragging = false, closedDrag = false, controlPending = false;
    struct Gesture
    {
        bool active = false;
        std::uint32_t id = 0, generation = 0;
        float value = 0;
    } gesture;
    juce::TextButton pluginTab{"Plugin"}, settingsTab{"Settings"}, parametersTab{"Parameters"},
        reset{"Reset"}, createAutomation{"Create automation clip"}, initialize{"Initialize"},
        removeInitialization{"Remove init"}, refresh{"Refresh parameters"}, lastTweaked{"Last tweaked"},
        applyIo{"Apply audio I/O"}, copyChain{"Copy chain"}, pasteChain{"Paste chain"},
        saveChain{"Save chain"}, loadChain{"Load chain"}, chainDirectory{"Chain directory"},
        detectorRouting{"Detector routing"};
    juce::ToggleButton bypass{"Bypass"}, notifications{"Native parameter notifications"};
    // Instrument Channel strip (familiar wrapper header): ON, PAN, VOL and the Mixer TRACK.
    juce::ToggleButton channelOn{"ON"};
    juce::Slider channelPan, channelVol, channelPitch;
    juce::TextButton rangeButton;
    juce::TextButton trackDown{"<"}, trackUp{">"};
    juce::Label trackLabel;
    juce::Label status, feedbackLabel, settingsHeading, mixLabel, parameterLabel, unavailable, ioHeading,
        inputBusLabel, outputBusLabel, inputMappingLabel, ioHint, chainHeading, chainHint;
    juce::ComboBox choices, inputBus, outputBus, inputMapping;
    juce::Slider mix, value;

    void configureSlider(juce::Slider &slider)
    {
        addAndMakeVisible(slider);
        slider.setSliderStyle(juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 90, 26);
        slider.setColour(juce::Slider::thumbColourId, juce::Colour(0xff70c2b5));
    }

    bool admit(const PluginWrapperIntent &intent)
    {
        const bool accepted = pluginWrapperIntentValid(intent) && send && send(intent);
        if (accepted && (intent.action != PluginWrapperAction::parameter || !intent.gestureOpen))
        {
            // Disable independent writes immediately, including bypass/mix. The
            // application ACK may be terminal before the worker GUI consumes it;
            // advertising Ready in that interval loses the next user action.
            controlPending = true;
            status.setText("Pending", juce::dontSendNotification);
        }
        feedbackLabel.setText(accepted ? "Pending application result" : "Request not admitted",
                              juce::dontSendNotification);
        refreshEnabled();
        return accepted;
    }

    bool independentReady() const
    {
        return state.ready && !state.pending && !controlPending && !gesture.active;
    }

    static bool sameCatalogue(const PluginBusCatalogue &a, const PluginBusCatalogue &b)
    {
        if (!a.valid() || !b.valid() || a.inputCount != b.inputCount || a.outputCount != b.outputCount)
            return false;
        for (int i = 0; i < a.inputCount + a.outputCount; ++i)
        {
            const auto &x = a.entries[std::size_t(i)], &y = b.entries[std::size_t(i)];
            if (x.index != y.index || x.input != y.input || x.defaultChannels != y.defaultChannels ||
                x.actualChannels != y.actualChannels ||
                juce::String::fromUTF8(x.name) != juce::String::fromUTF8(y.name))
                return false;
        }
        return true;
    }

    void refreshRoutingSelections()
    {
        inputBus.setSelectedId(routingUsable && routingCatalogue.inputCount > 0 && draftIo.inputBus >= 0 &&
                                       draftIo.inputBus < routingCatalogue.inputCount
                                   ? draftIo.inputBus + 1
                                   : 0,
                               juce::dontSendNotification);
        outputBus.setSelectedId(routingUsable && draftIo.outputBus >= 0 &&
                                        draftIo.outputBus < routingCatalogue.outputCount
                                    ? draftIo.outputBus + 1
                                    : 0,
                                juce::dontSendNotification);
        inputMapping.setSelectedId(draftIo.inputChannels == 2 && draftIo.monoSource == 0 ? 1
                                   : draftIo.inputChannels == 1 && draftIo.monoSource >= 1 &&
                                           draftIo.monoSource <= 3
                                       ? draftIo.monoSource + 1
                                       : 0,
                                   juce::dontSendNotification);
    }

    bool routingDraftValid() const
    {
        return routingUsable && draftIo.valid() && outputBus.getSelectedId() > 0 &&
               draftIo.outputBus < routingCatalogue.outputCount &&
               (routingCatalogue.inputCount == 0
                    ? draftIo.inputBus == authoritativeIo.inputBus &&
                          draftIo.inputChannels == authoritativeIo.inputChannels &&
                          draftIo.monoSource == authoritativeIo.monoSource
                    : inputBus.getSelectedId() > 0 && inputMapping.getSelectedId() > 0 &&
                          draftIo.inputBus < routingCatalogue.inputCount);
    }

    void chainAction(PluginWrapperAction action)
    {
        if (independentReady() && !state.instrument)
            admit({action, 0, 0, 0, false});
    }

    const PluginWrapperParameterView *lastTweakedParameter() const
    {
        const auto id = std::uint32_t(state.lastTweakedSelection);
        const auto generation = std::uint32_t(state.lastTweakedSelection >> 32);
        if (!generation)
            return nullptr;
        for (const auto &parameter : parameters)
            if (parameter.id == id && parameter.generation == generation)
                return &parameter;
        return nullptr;
    }

    void parameterAction(PluginWrapperAction action)
    {
        const auto *parameter = currentParameter();
        if (independentReady() && parameter && parameter->writable && parameter->automatable &&
            parameter->generation)
            admit({action, parameter->id, parameter->generation, parameter->value, false});
    }

    const PluginWrapperParameterView *currentParameter() const
    {
        return selected >= 0 && size_t(selected) < parameters.size() ? &parameters[size_t(selected)]
                                                                     : nullptr;
    }

    void refreshParameter()
    {
        const auto *parameter = currentParameter();
        parameterLabel.setText(parameter ? parameter->label + "  |  Normalized 0 to 1"
                                         : "No exposed parameter selected",
                               juce::dontSendNotification);
        if (!dragging && parameter)
            value.setValue(parameter->value, juce::dontSendNotification);
        refreshEnabled();
    }

    void refreshEnabled()
    {
        const bool idle = independentReady();
        bypass.setEnabled(idle);
        mix.setEnabled(idle && !state.instrument);
        mixLabel.setText(state.instrument ? "Wet / dry is available for effects only" : "Effect wet / dry",
                         juce::dontSendNotification);
        choices.setEnabled(idle && !dragging && !parameters.empty());
        const auto *parameter = currentParameter();
        value.setEnabled(parameter && parameter->writable && state.ready && (idle || gesture.active));
        reset.setEnabled(parameter && parameter->writable && parameter->hasDefault && idle && !dragging);
        const bool automation = parameter && parameter->writable && parameter->automatable &&
                                parameter->generation && idle && !dragging;
        createAutomation.setEnabled(automation);
        initialize.setEnabled(automation);
        removeInitialization.setEnabled(automation);
        refresh.setEnabled(idle && !dragging);
        lastTweaked.setEnabled(idle && !dragging && lastTweakedParameter());
        notifications.setEnabled(idle);
        inputBus.setEnabled(idle && routingUsable && routingCatalogue.inputCount > 0);
        outputBus.setEnabled(idle && routingUsable);
        inputMapping.setEnabled(idle && routingUsable && routingCatalogue.inputCount > 0);
        applyIo.setEnabled(idle && routingDraftValid() && draftIo != authoritativeIo);
        for (auto *button :
             {&copyChain, &pasteChain, &saveChain, &loadChain, &chainDirectory, &detectorRouting})
            button->setEnabled(idle && !state.instrument);
        chainHint.setText(
            state.instrument
                ? "Containing Mixer chain actions are available for effects only."
                : "Actions target this effect's containing chain; structural changes may close editors.",
            juce::dontSendNotification);
    }

    void applyView()
    {
        pluginTab.setToggleState(view == PluginWrapperView::plugin, juce::dontSendNotification);
        settingsTab.setToggleState(view == PluginWrapperView::settings, juce::dontSendNotification);
        parametersTab.setToggleState(view == PluginWrapperView::parameters, juce::dontSendNotification);
        if (native)
            native->setVisible(view == PluginWrapperView::plugin);
        unavailable.setVisible(view == PluginWrapperView::plugin && !native);
        for (auto *component :
             std::vector<juce::Component *>{&settingsHeading, &mixLabel,          &mix,
                                            &notifications,   &ioHeading,         &inputBusLabel,
                                            &outputBusLabel,  &inputMappingLabel, &inputBus,
                                            &outputBus,       &inputMapping,      &applyIo,
                                            &ioHint,          &chainHeading,      &chainHint,
                                            &copyChain,       &pasteChain,        &saveChain,
                                            &loadChain,       &chainDirectory,    &detectorRouting})
            component->setVisible(view == PluginWrapperView::settings);
        for (auto *component :
             std::vector<juce::Component *>{&choices, &parameterLabel, &value, &reset, &createAutomation,
                                            &initialize, &removeInitialization, &refresh, &lastTweaked})
            component->setVisible(view == PluginWrapperView::parameters);
    }

    void resizeForView()
    {
        const juce::ScopedValueSetter<bool> guard(choosingSize, true);
        if (view == PluginWrapperView::plugin && native)
            setSize(juce::jmax(560, naturalWidth), toolbarHeight + naturalHeight + footerHeight);
        else
            setSize(560, view == PluginWrapperView::settings ? 640 : 340);
        resized();
    }

    void componentMovedOrResized(juce::Component &component, bool, bool wasResized) override
    {
        if (&component != native || !wasResized || resizingNative)
            return;
        naturalWidth = juce::jmax(1, component.getWidth());
        naturalHeight = juce::jmax(1, component.getHeight());
        if (view == PluginWrapperView::plugin)
        {
            resizeForView();
            if (onNativeSizeChanged)
                onNativeSizeChanged();
        }
    }
};
} // namespace vibera
