// SPDX-License-Identifier: AGPL-3.0-or-later
#include "NativeEffectsEditor.h"
#include "dsp/StudioEffects.h"
#include "dsp/PitchCorrect.h"
#include "NativeEffectsVisuals.h"
#include <cmath>
#include <complex>

namespace vibera::effects
{
namespace
{
constexpr int headerHeight = 116, bottomHeight = 128;
// A lighter slate palette: raised cards and plates read clearly without a near-black ground.
const juce::Colour background(0xff24323c), backgroundTop(0xff31424f), cardFill(0xff364753), border(0xff4f6370),
    teal(0xff5fe6cf), warm(0xffffb35c), foreground(0xfff2f7f9), secondary(0xffb6c7d0), plate(0xff283842),
    well(0xff2a3943);
// Balanced grid: as few rows as fit, then as few columns as those rows need; a short last
// row is centred, so every layout is symmetric (9 -> 3x3, 7 -> 4+3, 11 -> 4+4+3).
struct Grid
{
    int columns = 1, lines = 1;
};
Grid balancedGrid(int count, int maximumColumns)
{
    Grid grid;
    maximumColumns = juce::jlimit(1, juce::jmax(1, count), maximumColumns);
    grid.lines = (count + maximumColumns - 1) / maximumColumns;
    grid.columns = (count + grid.lines - 1) / grid.lines;
    return grid;
}

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
    g.setColour(well);
    g.strokePath(track,
                 juce::PathStrokeType(5.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    active.addCentredArc(centre.x, centre.y, radius, radius, 0.f, start, angle, true);
    g.setColour(teal.withAlpha(slider.isEnabled() ? .95f : .3f));
    g.strokePath(active,
                 juce::PathStrokeType(4.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    auto face = bounds.reduced(9.f);
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff5d717d), face.getTopLeft(),
                                           juce::Colour(0xff34444f), face.getBottomRight(), false));
    g.fillEllipse(face);
    g.setColour(juce::Colour(0xff7b8f9a).withAlpha(.6f));
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
    g.setColour(well);
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
    g.setColour(plate);
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
    curve_ = spec.id == EffectId::parametricEq;
    reduction_ = spec.id == EffectId::deEsser || spec.id == EffectId::multiband || spec.id == EffectId::maximizer;
    spectrum_ = spec.id == EffectId::spectrumAnalyzer;
    // Every effect shows a live visual of what its controls change.
    const int displayHeight = (curve_ ? 210 : spectrum_ ? 320 : 190) + 10;
    const int count = int(spec.parameterCount);
    const int width = equalizer || curve_ || spectrum_ ? 1000 : count <= 2 ? 640 : 780;
    const int minimumWidth = equalizer ? 720 : 560;
    // The narrowest window has the most rows; the preferred width may need fewer.
    const auto narrowest = balancedGrid(count, equalizer ? count : (minimumWidth - 40) / 160);
    const auto preferred = balancedGrid(count, equalizer ? count : (width - 40) / 160);
    const auto heightFor = [&](const Grid &grid)
    { return headerHeight + bottomHeight + displayHeight + (equalizer ? 300 : grid.lines * 150); };
    setResizable(true, false);
    setResizeLimits(minimumWidth, heightFor(narrowest), 1440, 1400);
    visualMid_.resize(4096);
    visualSide_.resize(4096);
    setSize(width, juce::jmax(heightFor(narrowest), heightFor(preferred) + (equalizer ? 0 : 16)));
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
        // Short label: the text before any ":" (the rest documents values for MCP).
        const juce::String fullName(p.name);
        const auto shortName = juce::String(p.id) == "scale" ? juce::String("Scale")
                               : fullName.containsChar(':') ? fullName.upToFirstOccurrenceOf(":", false, false)
                               : fullName.upToFirstOccurrenceOf(" (", false, false);
        row->label.setText(shortName, juce::dontSendNotification);
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
        row->slider.setColour(juce::Slider::textBoxBackgroundColourId, plate);
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
        // Choice parameters read as words, not numbers.
        const juce::String id(p.id);
        if (spec.id == EffectId::pitchCorrect && id == "key")
            row->slider.textFromValueFunction = [](double v)
            {
                static const char *notes[]{"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
                return juce::String(notes[juce::jlimit(0, 11, int(std::lround(v)))]);
            };
        else if (spec.id == EffectId::pitchCorrect && id == "scale")
            row->slider.textFromValueFunction = [](double v)
            { return juce::String(PitchCorrect::scaleName(juce::jlimit(0, PitchCorrect::scaleCount - 1, int(std::lround(v))))); };
        else if (spec.id == EffectId::filter && id == "mode")
            row->slider.textFromValueFunction = [](double v)
            {
                static const char *modes[]{"Low pass", "High pass", "Band pass"};
                return juce::String(modes[juce::jlimit(0, 2, int(std::lround(v)))]);
            };
        else if (spec.id == EffectId::studioReverb && id == "type")
            row->slider.textFromValueFunction = [](double v)
            {
                static const char *types[]{"Room", "Plate", "Hall"};
                return juce::String(types[juce::jlimit(0, 2, int(std::lround(v)))]);
            };
        else if (spec.id == EffectId::beatGate && id == "pattern")
            row->slider.textFromValueFunction = [](double v)
            { return juce::String(BeatGate::patternName(juce::jlimit(0, BeatGate::patternCount - 1, int(std::lround(v))))); };
        else if (spec.id == EffectId::beatGate && (id == "step" || id == "stutter" || id == "hold" || id == "cycle"))
            row->slider.textFromValueFunction = [id](double v)
            {
                static const char *steps[]{"1/32", "1/16", "1/8", "1/4"};
                static const char *stutters[]{"Off", "1/4", "1/8", "1/16", "1/32"};
                static const char *holds[]{"1/2 beat", "1 beat", "2 beats"};
                static const char *cycles[]{"1 beat", "2 beats", "4 beats"};
                const int i = int(std::lround(v));
                return juce::String(id == "step"      ? steps[juce::jlimit(0, 3, i)]
                                    : id == "stutter" ? stutters[juce::jlimit(0, 4, i)]
                                    : id == "hold"    ? holds[juce::jlimit(0, 2, i)]
                                                      : cycles[juce::jlimit(0, 2, i)]);
            };
        else if (p.interval == 1 && p.minimum == 0 && p.maximum == 1)
            row->slider.textFromValueFunction = [](double v) { return juce::String(v >= .5 ? "On" : "Off"); };
        if (row->slider.textFromValueFunction)
            row->slider.setTextValueSuffix({});
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
    if (reduction_)
    {
        // Fast attack, slower fall so short reductions stay readable.
        const float now = instance_.gainReductionDb();
        shownReduction_ = now < shownReduction_ ? now : shownReduction_ + (now - shownReduction_) * .25f;
    }
    if (!display_.isEmpty())
        repaint(display_);
    const bool valid = instance_.lastRestoreAccepted();
    footer_.setText(valid ? "Double-click to reset  /  Drag to adjust  /  Values follow host automation"
                          : "Invalid saved state refused; controls preserved",
                    juce::dontSendNotification);
    footer_.setColour(juce::Label::textColourId, valid ? secondary : juce::Colour(0xffff977d));
}
void NativeEffectsEditor::paint(juce::Graphics &graphics)
{
    graphics.setGradientFill(juce::ColourGradient(backgroundTop, 0.f, 0.f, background,
                                                  float(getWidth()), float(getHeight()), false));
    graphics.fillAll();
    graphics.setColour(teal.withAlpha(.65f));
    graphics.fillRoundedRectangle(float(getWidth() - 90), 27.f, 8.f, 18.f, 3.f);
    graphics.fillRoundedRectangle(float(getWidth() - 76), 21.f, 8.f, 30.f, 3.f);
    graphics.fillRoundedRectangle(float(getWidth() - 62), 31.f, 8.f, 14.f, 3.f);
    for (const auto &row : rows_)
    {
        const auto card = row->card.toFloat();
        graphics.setColour(juce::Colour(0xff10181e).withAlpha(.22f));
        graphics.fillRoundedRectangle(card.translated(0.f, 3.f), 12.f);
        graphics.setColour(cardFill);
        graphics.fillRoundedRectangle(card, 12.f);
        graphics.setColour(border);
        graphics.drawRoundedRectangle(card.reduced(.5f), 12.f, 1.f);
        graphics.setColour(teal.withAlpha(.35f));
        graphics.drawHorizontalLine(row->card.getY() + 1, card.getX() + 14.f, card.getRight() - 14.f);
    }
    if (!display_.isEmpty())
        paintDisplay(graphics);
}
namespace
{
// In-place radix-2 FFT (independent textbook implementation) for the analyzer display.
void fft(std::vector<std::complex<float>> &a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(a[i], a[j]);
    }
    for (size_t length = 2; length <= n; length <<= 1)
    {
        const float angle = -2.f * juce::MathConstants<float>::pi / float(length);
        const std::complex<float> step(std::cos(angle), std::sin(angle));
        for (size_t i = 0; i < n; i += length)
        {
            std::complex<float> w(1.f, 0.f);
            for (size_t k = 0; k < length / 2; ++k)
            {
                const auto u = a[i + k], v = a[i + k + length / 2] * w;
                a[i + k] = u + v;
                a[i + k + length / 2] = u - v;
                w *= step;
            }
        }
    }
}
} // namespace
void NativeEffectsEditor::paintDisplay(juce::Graphics &g)
{
    const auto box = display_.toFloat();
    g.setColour(plate);
    g.fillRoundedRectangle(box, 12.f);
    g.setColour(border);
    g.drawRoundedRectangle(box.reduced(.5f), 12.f, 1.f);
    auto plot = box.reduced(16.f, 14.f);
    if (spectrum_)
    {
        constexpr int size = 4096;
        std::vector<float> samples(size);
        instance_.latestSamples(samples.data(), size);
        std::vector<std::complex<float>> bins(size);
        for (int i = 0; i < size; ++i)
            bins[size_t(i)] = {samples[size_t(i)] * (.5f - .5f * std::cos(2.f * juce::MathConstants<float>::pi * i / (size - 1))), 0.f};
        fft(bins);
        const double rate = instance_.getSampleRate() > 0 ? instance_.getSampleRate() : 48000.;
        const float slope = rows_.size() > 0 ? float(rows_[0]->slider.getValue()) : 3.f;
        const float falloff = rows_.size() > 1 ? float(rows_[1]->slider.getValue()) : .5f;
        spectrumDb_.resize(size / 2, -120.f);
        for (int k = 1; k < size / 2; ++k)
        {
            const double hz = k * rate / size;
            // Hann window coherent gain 0.5; slope tilts the display around 1 kHz.
            float db = juce::Decibels::gainToDecibels(std::abs(bins[size_t(k)]) * 4.f / size, -120.f) +
                       slope * float(std::log2(std::max(hz, 1.) / 1000.));
            auto &shown = spectrumDb_[size_t(k)];
            shown = db > shown ? db : shown - (1.f + 9.f * (1.f - falloff)) * .5f;
            shown = std::max(shown, -120.f);
        }
        const auto xOf = [&](double hz) { return plot.getX() + float(std::log(hz / 20.) / std::log(1000.)) * plot.getWidth(); };
        const auto yOf = [&](double db) { return plot.getY() + float(-db / 90.) * plot.getHeight(); };
        g.setFont(juce::FontOptions(10.f));
        for (double hz : {50., 100., 200., 500., 1000., 2000., 5000., 10000.})
        {
            g.setColour(border.withAlpha(.5f));
            g.drawVerticalLine(int(xOf(hz)), plot.getY(), plot.getBottom());
            g.setColour(secondary);
            g.drawText(hz >= 1000 ? juce::String(int(hz / 1000)) + "k" : juce::String(int(hz)), int(xOf(hz)) + 3,
                       int(plot.getBottom()) - 13, 32, 12, juce::Justification::centredLeft);
        }
        for (double db : {-12., -24., -36., -48., -60., -72.})
        {
            g.setColour(border.withAlpha(.45f));
            g.drawHorizontalLine(int(yOf(db)), plot.getX(), plot.getRight());
            g.setColour(secondary);
            g.drawText(juce::String(int(db)), int(plot.getX()) + 3, int(yOf(db)) - 13, 40, 12,
                       juce::Justification::centredLeft);
        }
        juce::Path line, fill;
        bool started = false;
        float lastX = -1;
        for (int k = 1; k < size / 2; ++k)
        {
            const double hz = k * rate / size;
            if (hz < 20 || hz > 20000)
                continue;
            const float x = xOf(hz);
            if (started && x - lastX < 1.5f)
                continue;
            const float y = juce::jlimit(plot.getY(), plot.getBottom(), yOf(spectrumDb_[size_t(k)]));
            if (!started)
            {
                line.startNewSubPath(x, y);
                fill.startNewSubPath(x, plot.getBottom());
                fill.lineTo(x, y);
                started = true;
            }
            else
            {
                line.lineTo(x, y);
                fill.lineTo(x, y);
            }
            lastX = x;
        }
        if (started)
        {
            fill.lineTo(lastX, plot.getBottom());
            fill.closeSubPath();
            g.setGradientFill(juce::ColourGradient(teal.withAlpha(.35f), 0.f, plot.getY(), teal.withAlpha(.03f), 0.f,
                                                   plot.getBottom(), false));
            g.fillPath(fill);
            g.setColour(teal);
            g.strokePath(line, juce::PathStrokeType(1.6f));
        }
        return;
    }
    if (curve_)
    {
        Values values{};
        for (size_t i = 0; i < rows_.size(); ++i)
            values[i] = float(rows_[i]->slider.getValue());
        const auto xOf = [&](double hz) { return plot.getX() + float(std::log(hz / 20.) / std::log(1000.)) * plot.getWidth(); };
        const auto yOf = [&](double db) { return plot.getCentreY() - float(db / 18.) * plot.getHeight() * .5f; };
        g.setFont(juce::FontOptions(10.f));
        for (double hz : {50., 100., 200., 500., 1000., 2000., 5000., 10000.})
        {
            g.setColour(border.withAlpha(.5f));
            g.drawVerticalLine(int(xOf(hz)), plot.getY(), plot.getBottom());
            g.setColour(secondary);
            g.drawText(hz >= 1000 ? juce::String(int(hz / 1000)) + "k" : juce::String(int(hz)),
                       int(xOf(hz)) + 3, int(plot.getBottom()) - 13, 32, 12, juce::Justification::centredLeft);
        }
        for (double db : {-12., -6., 0., 6., 12.})
        {
            g.setColour(db == 0 ? secondary.withAlpha(.6f) : border.withAlpha(.5f));
            g.drawHorizontalLine(int(yOf(db)), plot.getX(), plot.getRight());
            g.setColour(secondary);
            g.drawText(juce::String(int(db)) + " dB", int(plot.getX()) + 3, int(yOf(db)) - 13, 50, 12,
                       juce::Justification::centredLeft);
        }
        juce::Path curve, fill;
        for (int x = 0; x <= int(plot.getWidth()); x += 2)
        {
            const double hz = 20. * std::pow(1000., x / double(plot.getWidth()));
            const float y = juce::jlimit(plot.getY(), plot.getBottom(),
                                         yOf(StudioEffects::eqResponseDb(values, hz, 48000)));
            if (x == 0)
            {
                curve.startNewSubPath(plot.getX(), y);
                fill.startNewSubPath(plot.getX(), yOf(0));
            }
            curve.lineTo(plot.getX() + float(x), y);
            fill.lineTo(plot.getX() + float(x), y);
        }
        fill.lineTo(plot.getRight(), yOf(0));
        fill.closeSubPath();
        g.setColour(teal.withAlpha(.16f));
        g.fillPath(fill);
        g.setColour(teal);
        g.strokePath(curve, juce::PathStrokeType(2.2f, juce::PathStrokeType::curved));
        // Band handles at each control frequency.
        const std::array<std::pair<int, int>, 4> handles{{{1, 2}, {3, 4}, {6, 7}, {9, 10}}};
        for (const auto &[frequency, gain] : handles)
        {
            const float x = xOf(values[size_t(frequency)]), y = yOf(values[size_t(gain)]);
            g.setColour(plate);
            g.fillEllipse(x - 6.f, y - 6.f, 12.f, 12.f);
            g.setColour(teal);
            g.drawEllipse(x - 6.f, y - 6.f, 12.f, 12.f, 2.f);
        }
        return;
    }
    Values values{};
    for (size_t i = 0; i < rows_.size(); ++i)
        values[i] = float(rows_[i]->slider.getValue());
    instance_.latestStereo(visualMid_.data(), visualSide_.data(), int(visualMid_.size()));
    visuals::Live live;
    live.mid = visualMid_.data();
    live.side = visualSide_.data();
    live.count = int(visualMid_.size());
    live.inputPeak = instance_.inputPeak();
    live.outputPeak = std::max(0.f, std::max(instance_.peak(0), instance_.peak(1)));
    live.reductionDb = shownReduction_;
    live.beat = instance_.beatPosition();
    live.clock = juce::Time::getMillisecondCounterHiRes() / 1000.;
    live.rate = instance_.getSampleRate() > 0 ? instance_.getSampleRate() : 48000.;
    live.detectedHz = instance_.detectedHz();
    live.targetHz = instance_.targetHz();
    const visuals::Palette palette{teal, warm, foreground, secondary, border, plate};
    visuals::paint(g, plot, instance_.effectId(), values, live, palette);
    if (reduction_ && instance_.effectId() != EffectId::deEsser)
    {
        // Live gain reduction readout for the studio dynamics (bottom corner, clear of captions).
        auto badge = juce::Rectangle<float>(box.getRight() - 150.f, box.getBottom() - 36.f, 136.f, 22.f);
        g.setColour(warm.withAlpha(.18f));
        g.fillRoundedRectangle(badge, 6.f);
        g.setColour(warm);
        g.setFont(juce::FontOptions(12.f, juce::Font::bold));
        g.drawText("GR " + juce::String(shownReduction_, 1) + " dB", badge, juce::Justification::centred);
    }
}
void NativeEffectsEditor::resized()
{
    brand_.setBounds(24, 12, getWidth() - 130, 22);
    heading_.setBounds(22, 36, getWidth() - 120, 44);
    subtitle_.setBounds(24, 82, getWidth() - 48, 24);
    outputMeter_.setBounds(24, getHeight() - 112, getWidth() - 48, 76);
    footer_.setBounds(24, getHeight() - 28, getWidth() - 48, 20);
    auto area =
        juce::Rectangle<int>(20, headerHeight, getWidth() - 40, getHeight() - headerHeight - bottomHeight);
    display_ = area.removeFromTop(curve_ ? 210 : spectrum_ ? 320 : 190);
    area.removeFromTop(10);
    if (rows_.empty())
        return;
    const bool equalizer = instance_.effectId() == EffectId::graphicEq;
    const int count = int(rows_.size());
    const auto grid = balancedGrid(count, equalizer ? count : juce::jmin(4, area.getWidth() / 160));
    const int columns = grid.columns, lines = grid.lines;
    constexpr int gap = 10;
    const int cellWidth = (area.getWidth() - gap * (columns - 1)) / columns;
    const int cellHeight = (area.getHeight() - gap * (lines - 1)) / lines;
    for (std::size_t i = 0; i < rows_.size(); ++i)
    {
        auto &row = *rows_[i];
        const int line = int(i) / columns, inLine = juce::jmin(columns, count - line * columns);
        const int indent = (columns - inLine) * (cellWidth + gap) / 2; // Centre a short last row.
        row.card = {area.getX() + indent + (int(i) % columns) * (cellWidth + gap),
                    area.getY() + line * (cellHeight + gap), cellWidth, cellHeight};
        row.label.setBounds(row.card.reduced(8, 0).removeFromTop(38));
        row.slider.setBounds(row.card.withTrimmedTop(38).reduced(equalizer ? 3 : 8, 8));
    }
    repaint();
}
} // namespace vibera::effects
