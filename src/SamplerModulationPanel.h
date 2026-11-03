// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "SamplerModulationSettings.h"
#include "Theme.h"
#include <functional>

namespace vibera
{
// Message-thread projection of canonical parameters; no DSP or project ownership.
class SamplerModulationPanel final : public juce::Component
{
    // Popup input must never enter JUCE's normal slider/button/combo handlers.
    // A listener after base mouseDown cannot prevent a value change or toggle.
    template <class Control> class AutomationControl final : public Control
    {
        bool popupStroke = false;

      public:
        std::function<void()> onContext;
        void mouseDown(const juce::MouseEvent &event) override
        {
            if (event.mods.isPopupMenu())
            {
                popupStroke = true;
                // A secondary button during an existing left stroke does not
                // finish or replace that stroke's ownership.
                if (this->isEnabled() && !event.mods.isLeftButtonDown())
                {
                    const auto callback = onContext;
                    if (callback)
                        callback();
                }
                return;
            }
            popupStroke = false;
            Control::mouseDown(event);
        }
        void mouseDrag(const juce::MouseEvent &event) override
        {
            if (!popupStroke && !event.mods.isPopupMenu())
                Control::mouseDrag(event);
        }
        void mouseUp(const juce::MouseEvent &event) override
        {
            if (popupStroke)
            {
                popupStroke = false;
                return;
            }
            Control::mouseUp(event);
        }
        void mouseDoubleClick(const juce::MouseEvent &event) override
        {
            if (!event.mods.isPopupMenu())
                Control::mouseDoubleClick(event);
        }
    };
    juce::ComboBox target;
    AutomationControl<juce::ComboBox> shape;
    juce::Label heading, explanation;
    std::array<AutomationControl<juce::Slider>, samplerModulationParameters.size()> sliders;
    std::array<AutomationControl<juce::ToggleButton>, samplerModulationParameters.size()> flags;
    std::array<juce::Label, samplerModulationParameters.size()> labels;
    SamplerModulation current;
    int selected = 2;
    int syncedTarget = 3, syncedShape = 1;
    std::array<bool, samplerModulationParameters.size()> strokeActive{}, strokeSuppressed{};
    std::array<int, samplerModulationParameters.size()> strokeTarget{};
    bool updating = false, available = true;
    static juce::String text(std::string_view s)
    {
        return juce::String(s.data(), s.size());
    }
    void submit(size_t i, double value)
    {
        if (!updating && available && !strokeSuppressed[i] && onChange)
            onChange(strokeActive[i] ? strokeTarget[i] : selected, samplerModulationParameters[i], value);
    }

    void showContext(size_t parameter)
    {
        if (!updating && available && !(selected == 1 && parameter < 12) && onContext)
            onContext(selected, samplerModulationParameters[parameter]);
    }

  public:
    std::function<void(int, const SamplerModulationParameter &)> onContext;
    std::function<void(int)> onTarget;
    std::function<void(int, const SamplerModulationParameter &, double)> onChange;
    std::function<bool()> onBegin;
    std::function<void()> onEnd;
    SamplerModulationPanel()
    {
        setComponentID("instrument.sampler.modulation");
        addAndMakeVisible(heading);
        heading.setText("Envelopes / LFO", juce::dontSendNotification);
        heading.setFont(juce::Font(18.f));
        addAndMakeVisible(target);
        target.setComponentID("instrument.sampler.modulation.target");
        const std::array<juce::String, 5> names{"Volume", "Pan", "Mod X / Cutoff", "Mod Y / Resonance",
                                                "Pitch"};
        for (size_t i = 0; i < names.size(); ++i)
            target.addItem(names[i], int(i) + 1);
        target.setSelectedId(selected + 1, juce::dontSendNotification);
        target.onChange = [this]
        {
            if (!updating && onTarget)
                onTarget(target.getSelectedId() - 1);
        };
        addAndMakeVisible(explanation);
        explanation.setJustificationType(juce::Justification::topLeft);
        explanation.setFont(juce::Font(12.f));
        addAndMakeVisible(shape);
        shape.setComponentID("instrument.sampler.modulation.lfoShape");
        shape.addItem("Sine", 1);
        shape.addItem("Triangle", 2);
        shape.addItem("Square", 3);
        shape.onChange = [this] { submit(19, shape.getSelectedId() - 1); };
        shape.onContext = [this] { showContext(19); };
        for (size_t i = 0; i < samplerModulationParameters.size(); ++i)
        {
            const auto &p = samplerModulationParameters[i];
            if (p.boolean)
            {
                auto &flag = flags[i];
                addAndMakeVisible(flag);
                flag.setButtonText(text(p.label));
                flag.setComponentID("instrument.sampler.modulation." + text(p.id));
                flag.onClick = [this, i] { submit(i, flags[i].getToggleState() ? 1 : 0); };
                flag.onContext = [this, i] { showContext(i); };
            }
            else if (p.id != "lfoShape")
            {
                auto &slider = sliders[i];
                addAndMakeVisible(slider);
                addAndMakeVisible(labels[i]);
                labels[i].setText(text(p.label), juce::dontSendNotification);
                labels[i].setFont(juce::Font(12.f));
                slider.setComponentID("instrument.sampler.modulation." + text(p.id));
                slider.setSliderStyle(juce::Slider::LinearHorizontal);
                slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 68, 26);
                slider.setRange(p.minimum, p.maximum, .001);
                slider.setDoubleClickReturnValue(true, p.initial);
                if (p.maximum == 64)
                    slider.setSkewFactorFromMidPoint(p.id == "lfoPeriod" ? 1. : .5);
                slider.onDragStart = [this, i]
                {
                    const bool admitted = !onBegin || onBegin();
                    strokeActive[i] = true;
                    strokeSuppressed[i] = !admitted;
                    strokeTarget[i] = selected;
                    if (!admitted)
                        sliders[i].setValue(samplerModulationValue(current, samplerModulationParameters[i]),
                                            juce::dontSendNotification);
                };
                slider.onValueChange = [this, i] { submit(i, sliders[i].getValue()); };
                slider.onContext = [this, i] { showContext(i); };
                slider.onDragEnd = [this, i]
                {
                    strokeActive[i] = false;
                    strokeSuppressed[i] = false;
                    if (onEnd)
                        onEnd();
                };
            }
        }
    }
    void invalidateStroke() noexcept
    {
        for (size_t i = 0; i < strokeActive.size(); ++i)
            if (strokeActive[i])
                strokeSuppressed[i] = true;
    }
    void sync(const std::array<SamplerModulation, 5> &settings, int destination, bool enabled)
    {
        updating = true;
        selected = juce::jlimit(0, 4, destination);
        available = enabled;
        current = settings[size_t(selected)];
        const int pendingTarget = int(target.getSelectedIdAsValue().getValue());
        if (pendingTarget == syncedTarget || pendingTarget == selected + 1)
        {
            syncedTarget = selected + 1;
            target.setSelectedId(syncedTarget, juce::dontSendNotification);
        }
        target.setEnabled(enabled);
        const int pendingShape = int(shape.getSelectedIdAsValue().getValue());
        if (pendingShape == syncedShape || pendingShape == current.lfoShape + 1)
        {
            syncedShape = current.lfoShape + 1;
            shape.setSelectedId(syncedShape, juce::dontSendNotification);
        }
        shape.setEnabled(enabled);
        for (size_t i = 0; i < samplerModulationParameters.size(); ++i)
        {
            const auto &p = samplerModulationParameters[i];
            const bool env = i < 12;
            const bool usable = enabled && !(env && selected == 1);
            flags[i].setEnabled(usable);
            sliders[i].setEnabled(usable);
            labels[i].setEnabled(usable);
            if (p.boolean)
                flags[i].setToggleState(samplerModulationValue(current, p) != 0, juce::dontSendNotification);
            else if (p.id != "lfoShape" && (!strokeActive[i] || strokeSuppressed[i]))
                sliders[i].setValue(samplerModulationValue(current, p), juce::dontSendNotification);
            if (p.maximum == 64)
            {
                const bool tempo = i < 12 ? current.tempo : current.lfoTempo;
                sliders[i].setTextValueSuffix(tempo ? " beats" : " s");
            }
        }
        explanation.setText(
            selected == 1   ? "Pan uses LFO only. Amount0 is neutral; Global shares phase across notes."
            : selected == 0 ? "Full volume envelope replaces legacy ADSR when enabled and Amount is nonzero."
            : selected == 4 ? "Pitch depth1 =24 semitones. Envelope + LFO add to note/Channel tuning."
                            : "Signed envelope + LFO add to manual and note Mod values, then clamp0..1.",
            juce::dontSendNotification);
        updating = false;
        repaint();
    }
    void paint(juce::Graphics &g) override
    {
        g.setColour(bg);
        g.fillRoundedRectangle(getLocalBounds().toFloat(), 7.f);
        const float half = getWidth() * .5f;
        g.setColour(accent.withAlpha(.13f));
        g.fillRoundedRectangle(12.f, 84.f, half - 18, float(getHeight() - 96), 5.f);
        g.fillRoundedRectangle(half + 6, 84.f, half - 18, float(getHeight() - 96), 5.f);
        // Original envelope preview. Parameter controls are the editable surface.
        if (selected != 1)
        {
            g.setColour(accent.withAlpha(.8f));
            g.setFont(12.f);
            g.drawText("Envelope preview", int(half + 20), 398, int(half - 46), 20,
                       juce::Justification::centredLeft);
            auto area = juce::Rectangle<float>(half + 20, 424, half - 46, 96);
            juce::Path line;
            const double duration = std::max(.001, current.delay + current.attack + current.hold +
                                                       current.decay + .3 + current.release);
            auto point = [&](double t, double level)
            {
                return juce::Point<float>(area.getX() + float(t / duration) * area.getWidth(),
                                          area.getBottom() - float(level) * area.getHeight());
            };
            line.startNewSubPath(point(0, 0));
            double t = current.delay;
            line.lineTo(point(t, 0));
            auto segment = [&](double length, double start, double end, double tension)
            {
                for (int i = 1; i <= 32; ++i)
                {
                    const double x = i / 32.;
                    const double y = x + tension * x * (1 - x);
                    line.lineTo(point(t + x * length, start + (end - start) * y));
                }
                t += length;
            };
            segment(current.attack, 0, 1, current.attackTension);
            segment(current.hold, 1, 1, 0);
            segment(current.decay, 1, current.sustain, current.decayTension);
            segment(.3, current.sustain, current.sustain, 0);
            segment(current.release, current.sustain, 0, current.releaseTension);
            g.setColour(accent.withAlpha(current.enabled ? 1.f : .35f));
            g.strokePath(line, juce::PathStrokeType(1.8f));
        }
    }
    void resized() override
    {
        const int half = getWidth() / 2;
        heading.setBounds(16, 10, half - 24, 28);
        target.setBounds(half + 6, 10, half - 22, 28);
        explanation.setBounds(16, 45, getWidth() - 32, 34);
        flags[0].setBounds(22, 92, half - 32, 26);
        flags[1].setBounds(22, 120, half - 32, 26);
        int row = 0;
        for (size_t i = 2; i < 12; ++i)
        {
            labels[i].setBounds(22, 155 + row * 35, 98, 26);
            sliders[i].setBounds(122, 155 + row * 35, half - 142, 26);
            ++row;
        }
        for (size_t i = 12; i < 15; ++i)
            flags[i].setBounds(half + 20, 92 + int(i - 12) * 28, half - 36, 26);
        for (size_t i = 15; i < 19; ++i)
        {
            const int y = 184 + int(i - 15) * 44;
            labels[i].setBounds(half + 20, y, 84, 28);
            sliders[i].setBounds(half + 106, y, half - 126, 28);
        }
        shape.setBounds(half + 20, 364, half - 40, 28);
    }
};
} // namespace vibera
