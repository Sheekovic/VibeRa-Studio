// SPDX-License-Identifier: AGPL-3.0-or-later
#include "NativeEffectsEditor.h"
#include <cmath>

namespace vibera::effects
{
namespace
{
constexpr int headerHeight = 116, bottomHeight = 128;
const juce::Colour background(0xff141a20), cardFill(0xff202a33), border(0xff33424c), teal(0xff59e0c8),
    foreground(0xffe5edf1), secondary(0xff94aab6);

juce::String peakText(float gain)
{
    if (!std::isfinite(gain) || gain < 0)
        return "--";
    return gain <= 0 ? "-inf" : juce::String(juce::Decibels::gainToDecibels(gain, -96.f), 1);
}
} // namespace

void NativeEffectsEditor::ControlsLookAndFeel::drawRotarySlider(juce::Graphics &g, int x, int y, int width,
                                                                int height, float position, float start,
                                                                float end, juce::Slider &slider)
{
    auto bounds = juce::Rectangle<float>(float(x), float(y), float(width), float(height)).reduced(8.f);
    const float diameter = juce::jmin(bounds.getWidth(), bounds.getHeight());
    bounds = bounds.withSizeKeepingCentre(diameter, diameter);
    const auto centre = bounds.getCentre();
    const float radius = diameter * .5f, angle = start + position * (end - start);
    juce::Path track, active;
    track.addCentredArc(centre.x, centre.y, radius, radius, 0.f, start, end, true);
    g.setColour(juce::Colour(0xff11191f));
    g.strokePath(track,
                 juce::PathStrokeType(5.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    active.addCentredArc(centre.x, centre.y, radius, radius, 0.f, start, angle, true);
    g.setColour(teal.withAlpha(slider.isEnabled() ? .95f : .3f));
    g.strokePath(active,
                 juce::PathStrokeType(4.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    auto face = bounds.reduced(9.f);
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff455661), face.getTopLeft(),
                                           juce::Colour(0xff212d36), face.getBottomRight(), false));
    g.fillEllipse(face);
    g.setColour(juce::Colour(0xff596c77).withAlpha(.5f));
    g.drawEllipse(face, 1.f);
    const float pointerRadius = juce::jmax(1.f, radius - 15.f);
    const juce::Point<float> tip(centre.x + std::sin(angle) * pointerRadius,
                                 centre.y - std::cos(angle) * pointerRadius);
    const juce::Point<float> root(centre.x + std::sin(angle) * pointerRadius * .45f,
                                  centre.y - std::cos(angle) * pointerRadius * .45f);
    g.setColour(foreground);
    g.drawLine({root, tip}, 2.5f);
}

void NativeEffectsEditor::ControlsLookAndFeel::drawLinearSlider(juce::Graphics &g, int x, int y, int width,
                                                                int height, float sliderPosition,
                                                                float minimum, float maximum,
                                                                juce::Slider::SliderStyle style,
                                                                juce::Slider &slider)
{
    if (style != juce::Slider::LinearVertical)
    {
        LookAndFeel_V4::drawLinearSlider(g, x, y, width, height, sliderPosition, minimum, maximum, style,
                                         slider);
        return;
    }
    const float centre = float(x) + float(width) * .5f;
    g.setColour(juce::Colour(0xff11191f));
    g.fillRoundedRectangle(centre - 3.f, float(y), 6.f, float(height), 3.f);
    const float defaultPosition =
        float(y + height) -
        float(slider.valueToProportionOfLength(slider.getDoubleClickReturnValue())) * float(height);
    g.setColour(secondary.withAlpha(.65f));
    g.drawLine(centre - 12.f, defaultPosition, centre + 12.f, defaultPosition, 1.f);
    g.setColour(teal.withAlpha(.65f));
    g.fillRoundedRectangle(centre - 2.f, juce::jmin(defaultPosition, sliderPosition), 4.f,
                           juce::jmax(1.f, std::abs(defaultPosition - sliderPosition)), 2.f);
    g.setGradientFill(juce::ColourGradient(teal.brighter(.2f), centre, sliderPosition - 7.f,
                                           teal.darker(.35f), centre, sliderPosition + 7.f, false));
    g.fillRoundedRectangle(centre - 15.f, sliderPosition - 7.f, 30.f, 14.f, 4.f);
    g.setColour(juce::Colour(0xff152a29));
    g.drawHorizontalLine(int(sliderPosition), centre - 9.f, centre + 9.f);
}

void NativeEffectsEditor::StereoOutputMeter::update(float left, float right)
{
    levels_ = {left, right};
    if (left == -1.f || right == -1.f)
        setTooltip("Native output not processed yet; sample peaks unavailable");
    else
        setTooltip("Latest processed block output sample peak: L " + peakText(left) + " dBFS, R " +
                   peakText(right) + " dBFS");
    repaint();
}

void NativeEffectsEditor::StereoOutputMeter::paint(juce::Graphics &g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour(juce::Colour(0xff11191f));
    g.fillRoundedRectangle(bounds, 10.f);
    g.setColour(border);
    g.drawRoundedRectangle(bounds.reduced(.5f), 10.f, 1.f);
    g.setFont(juce::FontOptions(11.f, juce::Font::bold));
    g.setColour(secondary);
    g.drawText(levels_[0] < 0 || levels_[1] < 0 ? "OUTPUT / NOT PROCESSED" : "OUTPUT / SAMPLE PEAK", 14, 7,
               200, 16, juce::Justification::centredLeft);
    const int trackX = 34, trackWidth = juce::jmax(1, getWidth() - 134);
    for (int channel = 0; channel < 2; ++channel)
    {
        const int y = 28 + channel * 19;
        const float db = std::isfinite(levels_[size_t(channel)]) && levels_[size_t(channel)] >= 0
                             ? juce::Decibels::gainToDecibels(levels_[size_t(channel)], -60.f)
                             : -60.f;
        const float fraction = juce::jlimit(0.f, 1.f, (db + 60.f) / 60.f);
        g.setColour(secondary);
        g.setFont(juce::FontOptions(11.f, juce::Font::bold));
        g.drawText(channel == 0 ? "L" : "R", 14, y - 2, 16, 15, juce::Justification::centredLeft);
        g.setColour(cardFill);
        g.fillRoundedRectangle(float(trackX), float(y), float(trackWidth), 9.f, 3.f);
        g.setColour(levels_[size_t(channel)] > 1.f ? juce::Colour(0xffff977d) : teal);
        if (fraction > 0)
            g.fillRoundedRectangle(float(trackX), float(y), float(trackWidth) * fraction, 9.f, 3.f);
        g.setFont(juce::FontOptions(12.f));
        g.drawText(peakText(levels_[size_t(channel)]) + " dBFS", trackX + trackWidth + 10, y - 4, 80, 18,
                   juce::Justification::centredRight);
    }
}

NativeEffectsEditor::NativeEffectsEditor(NativeEffectInstance &instance)
    : AudioProcessorEditor(instance), instance_(instance)
{
    const auto &spec = effectSpec(instance.effectId());
    const bool equalizer = spec.id == EffectId::graphicEq;
    const int minimumColumns = juce::jmin(3, int(spec.parameterCount));
    const int minimumRows = (int(spec.parameterCount) + minimumColumns - 1) / minimumColumns;
    const int minimumHeight = equalizer ? 500 : headerHeight + bottomHeight + minimumRows * 164;
    setResizable(true, false);
    setResizeLimits(equalizer ? 720 : 560, minimumHeight, 1440, 1200);
    setSize(equalizer                  ? 1000
            : spec.parameterCount <= 2 ? 600
                                       : 780,
            juce::jmax(minimumHeight, equalizer ? 540 : 510));
    brand_.setText("V I B E R A   /   E F F E C T S", juce::dontSendNotification);
    brand_.setFont(juce::FontOptions(11.f, juce::Font::bold));
    brand_.setColour(juce::Label::textColourId, teal);
    heading_.setText(instance.getName(), juce::dontSendNotification);
    heading_.setFont(juce::FontOptions(30.f, juce::Font::bold));
    heading_.setColour(juce::Label::textColourId, foreground);
    subtitle_.setText(juce::String(spec.category).toUpperCase() + "   /   STEREO" +
                          (spec.id == EffectId::limiter ? "   /   ZERO LOOKAHEAD" : ""),
                      juce::dontSendNotification);
    subtitle_.setFont(juce::FontOptions(12.f));
    subtitle_.setColour(juce::Label::textColourId, secondary);
    footer_.setFont(juce::FontOptions(11.f));
    footer_.setColour(juce::Label::textColourId, secondary);
    outputMeter_.setComponentID("nativeEffects.outputMeter");
    outputMeter_.setName("Stereo output sample peak");
    for (auto *component :
         std::array<juce::Component *, 5>{&brand_, &heading_, &subtitle_, &outputMeter_, &footer_})
        addAndMakeVisible(component);
    rows_.reserve(spec.parameterCount);
    for (std::size_t i = 0; i < spec.parameterCount; ++i)
    {
        const auto &p = spec.parameters[i];
        auto row = std::make_unique<Row>();
        row->parameter = instance.getHostedParameter(int(i));
        row->range = {p.minimum, p.maximum, p.interval, p.skew};
        row->label.setText(p.name, juce::dontSendNotification);
        row->label.setFont(juce::FontOptions(equalizer ? 12.f : 14.f, juce::Font::bold));
        row->label.setJustificationType(juce::Justification::centred);
        row->label.setColour(juce::Label::textColourId, foreground);
        row->slider.setComponentID("nativeEffects.parameter." + juce::String(p.id));
        row->slider.setName(p.name);
        row->slider.setLookAndFeel(&controlsLookAndFeel_);
        row->slider.setSliderStyle(equalizer ? juce::Slider::LinearVertical
                                             : juce::Slider::RotaryVerticalDrag);
        row->slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, equalizer ? 74 : 126, 26);
        row->slider.setColour(juce::Slider::textBoxTextColourId, teal);
        row->slider.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xff172128));
        row->slider.setColour(juce::Slider::textBoxOutlineColourId, border);
        const juce::String unit(p.unit);
        row->slider.setTextValueSuffix(unit.isEmpty() ? "" : " " + unit);
        row->slider.setTooltip(juce::String(p.name) + " / Default " + juce::String(p.initial) +
                               (unit.isEmpty() ? "" : " " + unit) + ". Double-click to reset.");
        row->slider.setNumDecimalPlacesToDisplay(p.interval > 0                                 ? 0
                                                 : unit == "Hz" && p.minimum < 1                ? 3
                                                 : unit == "Hz" || unit == "ms" || unit == "dB" ? 1
                                                 : juce::String(p.id) == "q"                    ? 3
                                                                                                : 2);
        row->slider.setNormalisableRange(
            juce::NormalisableRange<double>(p.minimum, p.maximum, p.interval, p.skew));
        row->slider.setDoubleClickReturnValue(true, p.initial);
        row->slider.setValue(row->range.convertFrom0to1(row->parameter->getValue()),
                             juce::dontSendNotification);
        auto *control = row.get();
        row->slider.onDragStart = [control]
        {
            if (!control->gesture)
            {
                control->gesture = true;
                control->parameter->beginChangeGesture();
            }
        };
        row->slider.onDragEnd = [control]
        {
            if (control->gesture)
            {
                control->parameter->endChangeGesture();
                control->gesture = false;
            }
        };
        row->slider.onValueChange = [control]
        {
            const bool singleChange = !control->gesture;
            if (singleChange)
                control->parameter->beginChangeGesture();
            const auto value = control->range.snapToLegalValue(float(control->slider.getValue()));
            control->parameter->setValueNotifyingHost(control->range.convertTo0to1(value));
            if (singleChange)
                control->parameter->endChangeGesture();
        };
        addAndMakeVisible(row->label);
        addAndMakeVisible(row->slider);
        rows_.push_back(std::move(row));
    }
    resized();
    timerCallback();
    startTimerHz(30);
}
NativeEffectsEditor::~NativeEffectsEditor()
{
    stopTimer();
    for (auto &row : rows_)
    {
        row->slider.onDragStart = nullptr;
        row->slider.onDragEnd = nullptr;
        row->slider.onValueChange = nullptr;
        row->slider.setLookAndFeel(nullptr);
        if (row->gesture)
            row->parameter->endChangeGesture();
    }
}
void NativeEffectsEditor::timerCallback()
{
    for (auto &row : rows_)
        if (!row->gesture)
            row->slider.setValue(row->range.convertFrom0to1(row->parameter->getValue()),
                                 juce::dontSendNotification);
    outputMeter_.update(instance_.peak(0), instance_.peak(1));
    const bool valid = instance_.lastRestoreAccepted();
    footer_.setText(valid ? "Double-click to reset  /  Drag to adjust  /  Values follow host automation"
                          : "Invalid saved state refused; controls preserved",
                    juce::dontSendNotification);
    footer_.setColour(juce::Label::textColourId, valid ? secondary : juce::Colour(0xffff977d));
}
void NativeEffectsEditor::paint(juce::Graphics &graphics)
{
    graphics.setGradientFill(juce::ColourGradient(juce::Colour(0xff202d36), 0.f, 0.f, background,
                                                  float(getWidth()), float(getHeight()), false));
    graphics.fillAll();
    graphics.setColour(teal.withAlpha(.65f));
    graphics.fillRoundedRectangle(float(getWidth() - 90), 27.f, 8.f, 18.f, 3.f);
    graphics.fillRoundedRectangle(float(getWidth() - 76), 21.f, 8.f, 30.f, 3.f);
    graphics.fillRoundedRectangle(float(getWidth() - 62), 31.f, 8.f, 14.f, 3.f);
    for (const auto &row : rows_)
    {
        const auto card = row->card.toFloat();
        graphics.setColour(juce::Colour(0xff0c1217).withAlpha(.35f));
        graphics.fillRoundedRectangle(card.translated(0.f, 3.f), 12.f);
        graphics.setColour(cardFill);
        graphics.fillRoundedRectangle(card, 12.f);
        graphics.setColour(border);
        graphics.drawRoundedRectangle(card.reduced(.5f), 12.f, 1.f);
        graphics.setColour(teal.withAlpha(.35f));
        graphics.drawHorizontalLine(row->card.getY() + 1, card.getX() + 14.f, card.getRight() - 14.f);
    }
}
void NativeEffectsEditor::resized()
{
    brand_.setBounds(24, 12, getWidth() - 130, 22);
    heading_.setBounds(22, 36, getWidth() - 120, 44);
    subtitle_.setBounds(24, 82, getWidth() - 48, 24);
    outputMeter_.setBounds(24, getHeight() - 112, getWidth() - 48, 76);
    footer_.setBounds(24, getHeight() - 28, getWidth() - 48, 20);
    const auto area =
        juce::Rectangle<int>(20, headerHeight, getWidth() - 40, getHeight() - headerHeight - bottomHeight);
    if (rows_.empty())
        return;
    const bool equalizer = instance_.effectId() == EffectId::graphicEq;
    const int count = int(rows_.size());
    const int columns = equalizer ? count : juce::jlimit(1, juce::jmin(4, count), area.getWidth() / 160);
    const int lines = (count + columns - 1) / columns;
    constexpr int gap = 10;
    const int cellWidth = (area.getWidth() - gap * (columns - 1)) / columns;
    const int cellHeight = (area.getHeight() - gap * (lines - 1)) / lines;
    for (std::size_t i = 0; i < rows_.size(); ++i)
    {
        auto &row = *rows_[i];
        row.card = {area.getX() + (int(i) % columns) * (cellWidth + gap),
                    area.getY() + (int(i) / columns) * (cellHeight + gap), cellWidth, cellHeight};
        row.label.setBounds(row.card.reduced(8, 0).removeFromTop(38));
        row.slider.setBounds(row.card.withTrimmedTop(38).reduced(equalizer ? 3 : 8, 8));
    }
    repaint();
}
} // namespace vibera::effects
