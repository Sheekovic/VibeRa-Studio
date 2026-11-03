// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Studio.h"
namespace vibera
{
class RackSwingControl final : public juce::Slider
{
    Studio &s;
    juce::String session;
    std::uint64_t serial = 0;
    bool owns = false;
    bool ownsGesture() const
    {
        return owns && s.controller.hasGesture() && s.controller.gestureOwner().isEmpty() &&
               s.controller.gestureSerial() == serial && s.controller.projectIdentity == session;
    }

  public:
    bool gestureActive() const
    {
        return ownsGesture();
    }
    explicit RackSwingControl(Studio &owner) : s(owner)
    {
        setComponentID("rack.swing");
        setRange(0, 100, .1);
        setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        setTextBoxStyle(juce::Slider::TextBoxLeft, false, 48, 24);
        setTextValueSuffix("%");
        setDoubleClickReturnValue(true, 0);
        setTooltip(
            "Global Swing | Alternate sixteenths; Channel Swing Mix multiplies it | Double-click: reset");
        onDragStart = [this]
        {
            if (s.finishRackVelocity().failed() || finish().failed())
                return;
            if (s.controller.hasGesture() && s.controller.gestureOwner().isEmpty())
            {
                s.status = "Finish or cancel the current edit before changing Swing";
                s.repaint();
                return;
            }
            if (auto admitted = s.stepVelocityAdmission(); admitted.failed())
            {
                s.status = admitted.getErrorMessage();
                return;
            }
            owns = s.controller.beginGesture();
            serial = s.controller.gestureSerial();
            session = s.controller.projectIdentity;
        };
        onDragEnd = [this] { finish(); };
        onValueChange = [this]
        {
            if (s.controller.hasGesture() && s.controller.gestureOwner().isEmpty() && !ownsGesture())
            {
                s.status = "Finish or cancel the current edit before changing Swing";
                sync();
                s.repaint();
                return;
            }
            if (auto admitted = s.stepVelocityAdmission(); admitted.failed())
            {
                s.status = admitted.getErrorMessage();
                sync();
                return;
            }
            CommandArgs args;
            args.value = getValue() / 100;
            s.command("project.swing", args);
        };
    }
    ~RackSwingControl() override
    {
        finish();
    }
    juce::Result finish()
    {
        if (ownsGesture())
        {
            if (auto result = s.controller.endGestureChecked("Global Swing"); result.failed())
            {
                s.status = result.getErrorMessage();
                s.repaint();
                return result; // Retry completion or cancel the original owned baseline.
            }
        }
        owns = false;
        return juce::Result::ok();
    }
    void sync()
    {
        if (!isMouseButtonDown())
            setValue(s.controller.project.swing * 100, juce::dontSendNotification);
    }
    void resized() override
    {
        finish();
        juce::Slider::resized();
    }
    void paint(juce::Graphics &g) override
    {
        const auto bounds = getLocalBounds().toFloat().removeFromRight(28).reduced(3, 2);
        const auto centre = bounds.getCentre();
        const auto radius = std::min(bounds.getWidth(), bounds.getHeight()) * .5f;
        g.setColour(grid);
        g.fillEllipse(centre.x - radius, centre.y - radius, radius * 2, radius * 2);
        const auto start = juce::MathConstants<float>::pi * 1.25f;
        const auto end = start + float(getValue() / 100) * juce::MathConstants<float>::pi * 1.5f;
        juce::Path arc;
        arc.addCentredArc(centre.x, centre.y, radius - 1, radius - 1, 0, start, end, true);
        g.setColour(accent);
        g.strokePath(arc, juce::PathStrokeType(2));
        g.drawLine(centre.x, centre.y, centre.x + std::sin(end) * (radius - 3),
                   centre.y - std::cos(end) * (radius - 3), 2);
    }
};
} // namespace vibera
