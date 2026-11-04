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
    // Target tabs in display order (Panning first, like the familiar layout); values are targets.
    static constexpr std::array<int, 5> tabOrder{1, 0, 2, 3, 4};
    juce::Rectangle<int> tabBounds(size_t index) const
    {
        static constexpr std::array<int, 5> widths{62, 58, 50, 50, 44};
        int x = 10;
        for (size_t i = 0; i < index; ++i)
            x += widths[i] + 8;
        return {x, 4, widths[index], 24};
    }
    juce::Rectangle<int> envelopeBox() const
    {
        return {6, 34, juce::roundToInt(getWidth() * .6f) - 9, getHeight() - 58};
    }
    juce::Rectangle<int> lfoBox() const
    {
        const auto left = envelopeBox();
        return {left.getRight() + 6, left.getY(), getWidth() - left.getRight() - 12, left.getHeight()};
    }
    static juce::String caption(size_t i)
    {
        static const std::array<const char *, 20> names{"", "", "DELAY", "ATT", "HOLD", "DEC", "SUS",
                                                        "REL", "A TENS", "D TENS", "R TENS", "AMT", "", "",
                                                        "", "DELAY", "ATT", "SPEED", "AMT", ""};
        return names[i];
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
        addChildComponent(target); // Kept for its stable ID; the tabs choose the target.
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
        shape.setSelectedId(syncedShape, juce::dontSendNotification);
        shape.onChange = [this] { submit(19, shape.getSelectedId() - 1); };
        shape.onContext = [this] { showContext(19); };
        for (size_t i = 0; i < samplerModulationParameters.size(); ++i)
        {
            const auto &p = samplerModulationParameters[i];
            if (p.boolean)
            {
                auto &flag = flags[i];
                addAndMakeVisible(flag);
                flag.setButtonText(i == 0                ? juce::String("Envelope")
                                   : i == 12             ? juce::String("LFO")
                                   : i == 1 || i == 13   ? juce::String("Tempo")
                                   : i == 14             ? juce::String("Global")
                                                         : text(p.label));
                flag.setTooltip(text(p.label));
                flag.setComponentID("instrument.sampler.modulation." + text(p.id));
                flag.onClick = [this, i] { submit(i, flags[i].getToggleState() ? 1 : 0); };
                flag.onContext = [this, i] { showContext(i); };
            }
            else if (p.id != "lfoShape")
            {
                auto &slider = sliders[i];
                addAndMakeVisible(slider);
                addAndMakeVisible(labels[i]);
                labels[i].setText(caption(i), juce::dontSendNotification);
                labels[i].setFont(juce::Font(10.5f, juce::Font::bold));
                labels[i].setJustificationType(juce::Justification::centred);
                labels[i].setColour(juce::Label::textColourId, muted);
                labels[i].setInterceptsMouseClicks(false, false);
                slider.setComponentID("instrument.sampler.modulation." + text(p.id));
                slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
                slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
                slider.setPopupDisplayEnabled(true, true, nullptr);
                slider.setTooltip(text(p.label));
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
    void mouseDown(const juce::MouseEvent &e) override
    {
        for (size_t i = 0; i < tabOrder.size(); ++i)
            if (tabBounds(i).contains(e.getPosition()) && available && !updating && onTarget)
                onTarget(tabOrder[i]);
    }
    void paint(juce::Graphics &g) override
    {
        g.setColour(bg);
        g.fillRoundedRectangle(getLocalBounds().toFloat(), 7.f);
        static const std::array<const char *, 5> tabNames{"Panning", "Volume", "Mod X", "Mod Y", "Pitch"};
        for (size_t i = 0; i < tabOrder.size(); ++i)
        {
            const auto r = tabBounds(i);
            const bool on = tabOrder[i] == selected;
            if (on)
            {
                g.setColour(accent.withAlpha(.16f));
                g.fillRoundedRectangle(r.toFloat(), 4.f);
            }
            g.setColour(on ? accent : available ? ink.withAlpha(.75f) : muted.withAlpha(.5f));
            g.setFont(juce::Font(13.f, on ? juce::Font::bold : juce::Font::plain));
            g.drawText(tabNames[i], r, juce::Justification::centred);
        }
        for (const auto box : {envelopeBox(), lfoBox()})
        {
            g.setColour(panel);
            g.fillRoundedRectangle(box.toFloat(), 5.f);
            g.setColour(grid);
            g.drawRoundedRectangle(box.toFloat().reduced(.5f), 5.f, 1.f);
        }
        auto graph = [](juce::Rectangle<int> box)
        {
            return juce::Rectangle<float>(float(box.getX() + 10), float(box.getY() + 34), float(box.getWidth() - 20),
                                          92.f);
        };
        for (const auto area : {graph(envelopeBox()), graph(lfoBox())})
        {
            g.setColour(bg.darker(.2f));
            g.fillRoundedRectangle(area, 4.f);
        }
        // Original envelope preview. Parameter controls are the editable surface.
        if (selected != 1)
        {
            const auto area = graph(envelopeBox()).reduced(6.f, 8.f);
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
            std::vector<juce::Point<float>> handles{point(t, 0)};
            auto segment = [&](double length, double start, double end, double tension)
            {
                for (int i = 1; i <= 32; ++i)
                {
                    const double x = i / 32.;
                    const double y = x + tension * x * (1 - x);
                    line.lineTo(point(t + x * length, start + (end - start) * y));
                }
                t += length;
                handles.push_back(point(t, end));
            };
            segment(current.attack, 0, 1, current.attackTension);
            segment(current.hold, 1, 1, 0);
            segment(current.decay, 1, current.sustain, current.decayTension);
            segment(.3, current.sustain, current.sustain, 0);
            segment(current.release, current.sustain, 0, current.releaseTension);
            const auto colour = accent.withAlpha(current.enabled ? 1.f : .35f);
            g.setColour(colour);
            g.strokePath(line, juce::PathStrokeType(1.8f));
            for (const auto handle : handles)
            {
                g.setColour(colour.withAlpha(.35f));
                g.fillEllipse(handle.x - 4.f, handle.y - 4.f, 8.f, 8.f);
                g.setColour(colour);
                g.drawEllipse(handle.x - 4.f, handle.y - 4.f, 8.f, 8.f, 1.2f);
            }
        }
        else
        {
            g.setColour(muted.withAlpha(.6f));
            g.setFont(12.f);
            g.drawText("Panning uses the LFO only", graph(envelopeBox()).toNearestInt(),
                       juce::Justification::centred);
        }
        const auto area = graph(lfoBox()).reduced(6.f, 10.f);
        juce::Path wave;
        const float amount = current.lfoEnabled ? std::max(.15f, float(std::abs(current.lfoAmount))) : .6f;
        for (int i = 0; i <= 96; ++i)
        {
            const float x = float(i) / 96.f, phase = x * 2.f;
            const float f = phase - std::floor(phase);
            const float v = current.lfoShape == 0   ? std::sin(f * juce::MathConstants<float>::twoPi)
                            : current.lfoShape == 1 ? 1.f - 4.f * std::abs(f - .5f)
                                                    : (f < .5f ? 1.f : -1.f);
            const juce::Point<float> p(area.getX() + x * area.getWidth(),
                                       area.getCentreY() - v * amount * area.getHeight() * .5f);
            if (i == 0)
                wave.startNewSubPath(p);
            else
                wave.lineTo(p);
        }
        g.setColour(accent.withAlpha(current.lfoEnabled ? 1.f : .35f));
        g.strokePath(wave, juce::PathStrokeType(1.6f));
    }
    void resized() override
    {
        heading.setVisible(false);
        const auto env = envelopeBox(), lfo = lfoBox();
        flags[0].setBounds(env.getX() + 8, env.getY() + 5, 110, 24);
        flags[1].setBounds(env.getRight() - 84, env.getY() + 5, 78, 24);
        auto knobRow = [this](juce::Rectangle<int> box, int y, std::initializer_list<size_t> items, int columns)
        {
            const int width = (box.getWidth() - 16) / columns;
            int x = box.getX() + 8;
            for (const auto i : items)
            {
                sliders[i].setBounds(x + (width - 40) / 2, y, 40, 40);
                labels[i].setBounds(x, y + 40, width, 14);
                x += width;
            }
        };
        const int firstRow = env.getY() + 134;
        knobRow(env, firstRow, {2, 3, 4, 5, 6, 7}, 6);
        knobRow(env, firstRow + 60, {8, 9, 10, 11}, 6);
        flags[12].setBounds(lfo.getX() + 8, lfo.getY() + 5, 70, 24);
        shape.setBounds(lfo.getRight() - 96, lfo.getY() + 5, 88, 24);
        knobRow(lfo, firstRow, {15, 16, 18, 17}, 4);
        flags[13].setBounds(lfo.getX() + 10, firstRow + 64, 76, 24);
        flags[14].setBounds(lfo.getX() + 90, firstRow + 64, 80, 24);
        explanation.setFont(juce::Font(11.f));
        explanation.setBounds(10, getHeight() - 22, getWidth() - 20, 18);
    }
};
} // namespace vibera
