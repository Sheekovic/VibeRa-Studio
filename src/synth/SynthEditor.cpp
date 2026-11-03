// SPDX-License-Identifier: AGPL-3.0-or-later
#include "SynthEditor.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>
#include <vector>

namespace vibera::synth
{
namespace
{
// The synthesizer has its own instrument identity (graphite + amber, one colour
// per module) instead of mirroring the host workspace theme.
const juce::Colour background(0xff0d0e12), panel(0xff15161c), panelTop(0xff1b1c24), inset(0xff0a0b0e),
    border(0xff2a2b35), accent(0xffffa53d), selected(0xff2e2414), foreground(0xffeeebe5), muted(0xff8e8b96);
const juce::Colour oscColour(0xffffa53d), unisonColour(0xffffc857), filterColour(0xffff6f5e),
    envColour(0xff4cc9f0), voiceColour(0xff7ee0a1), fxColour(0xffb794ff), macroColour(0xffffa53d);
juce::FontOptions uiFont(float size = 13.0f, bool bold = false)
{
    return juce::FontOptions("Segoe UI", size, bold ? juce::Font::bold : juce::Font::plain);
}
// Instrument display face (headings, values, preset display): a stock Windows font.
juce::FontOptions displayFont(float size, bool bold = false)
{
    return juce::FontOptions("Bahnschrift", size, bold ? juce::Font::bold : juce::Font::plain);
}
constexpr int cardHeader = 30;
constexpr float pi = std::numbers::pi_v<float>;

juce::String star(bool filled)
{
    return juce::String::charToString(filled ? 0x2605 : 0x2606);
}
bool sameBrowser(const SynthBrowserState &a, const SynthBrowserState &b)
{
    return a.genre == b.genre && a.family == b.family && a.search == b.search &&
           a.favouritesOnly == b.favouritesOnly && a.favouriteIds == b.favouriteIds &&
           a.effectPage == b.effectPage && a.octave == b.octave && a.scrollX == b.scrollX &&
           a.scrollY == b.scrollY && a.presetScrollY == b.presetScrollY &&
           a.selectedPresetId == b.selectedPresetId;
}
void card(juce::Graphics &g, juce::Rectangle<int> bounds, const juce::String &title = {},
          juce::Colour colour = accent)
{
    // Module panel: soft vertical gradient, a coloured module tab and a header rule.
    if (bounds.isEmpty())
        return;
    const auto area = bounds.toFloat();
    g.setGradientFill(juce::ColourGradient(panelTop, 0, area.getY(), panel, 0, area.getBottom(), false));
    g.fillRoundedRectangle(area, 7);
    g.setColour(border);
    g.drawRoundedRectangle(area.reduced(.5f), 7, 1);
    if (title.isNotEmpty())
    {
        auto heading = bounds.removeFromTop(cardHeader).reduced(12, 0);
        g.setColour(colour);
        g.fillRoundedRectangle(heading.removeFromLeft(4).withSizeKeepingCentre(4, 12).toFloat(), 2);
        heading.removeFromLeft(8);
        g.setColour(foreground.withAlpha(.9f));
        g.setFont(displayFont(13, true).withKerningFactor(.08f));
        g.drawText(title, heading, juce::Justification::centredLeft);
        g.setColour(border.withAlpha(.7f));
        g.fillRect(bounds.getX() + 10, bounds.getY(), bounds.getWidth() - 20, 1);
    }
}
juce::Colour moduleColour(ParameterId id)
{
    switch (id)
    {
    case ParameterId::tone:
    case ParameterId::warmth:
    case ParameterId::space:
    case ParameterId::motion:
        return macroColour;
    case ParameterId::unisonVoices:
    case ParameterId::unisonDetuneCents:
    case ParameterId::unisonSpread:
    case ParameterId::subLevel:
    case ParameterId::noiseLevel:
    case ParameterId::analogDrift:
        return unisonColour;
    case ParameterId::cutoffHz:
    case ParameterId::resonance:
    case ParameterId::filterDrive:
    case ParameterId::filterEnvelopeOctaves:
    case ParameterId::filterKeyTrack:
    case ParameterId::filterVelocityOctaves:
        return filterColour;
    case ParameterId::attackMs:
    case ParameterId::decayMs:
    case ParameterId::sustain:
    case ParameterId::releaseMs:
    case ParameterId::envelopeCurve:
    case ParameterId::velocityToAmp:
    case ParameterId::modAttackMs:
    case ParameterId::modDecayMs:
    case ParameterId::modSustain:
        return envColour;
    case ParameterId::glideMs:
    case ParameterId::polyphony:
    case ParameterId::bendRangeSemitones:
    case ParameterId::vibratoRateHz:
    case ParameterId::vibratoDepthCents:
    case ParameterId::pan:
    case ParameterId::pitchBend:
    case ParameterId::modWheel:
        return voiceColour;
    case ParameterId::eqLowDb:
    case ParameterId::eqMidDb:
    case ParameterId::eqHighDb:
    case ParameterId::compressorThresholdDb:
    case ParameterId::compressorRatio:
    case ParameterId::chorusMix:
    case ParameterId::delayTimeMs:
    case ParameterId::delayFeedback:
    case ParameterId::delayMix:
    case ParameterId::reverbSize:
    case ParameterId::reverbDecaySeconds:
    case ParameterId::reverbMix:
        return fxColour;
    default:
        return oscColour;
    }
}
void styleLabel(juce::Label &label, float size, juce::Colour colour, bool bold = false)
{
    label.setFont(uiFont(size, bold));
    label.setColour(juce::Label::textColourId, colour);
}
// One plain-language sentence per control; the editor appends units/defaults.
const char *parameterHelp(ParameterId id)
{
    switch (id)
    {
    case ParameterId::tone:
        return "Macro: opens or darkens the filter around the patch's own cutoff.";
    case ParameterId::warmth:
        return "Macro: adds low-end weight and trims the output to match.";
    case ParameterId::space:
        return "Macro: adds reverb and echo ambience within this patch's limits.";
    case ParameterId::motion:
        return "Macro: slow filter movement and gentle pitch drift at the LFO rate.";
    case ParameterId::attackMs:
        return "Time for the amplitude to rise after a key press.";
    case ParameterId::decayMs:
        return "Time to fall from the attack peak to the sustain level.";
    case ParameterId::sustain:
        return "Level held while the key stays down.";
    case ParameterId::releaseMs:
        return "Fade time after the key is released.";
    case ParameterId::cutoffHz:
        return "Filter cutoff frequency; lower values sound darker.";
    case ParameterId::resonance:
        return "Emphasis at the cutoff; high values on the 24 dB ladder ring and whistle.";
    case ParameterId::eqLowDb:
        return "Low shelf gain of the built-in EQ.";
    case ParameterId::eqMidDb:
        return "Mid band gain of the built-in EQ.";
    case ParameterId::eqHighDb:
        return "High shelf gain of the built-in EQ.";
    case ParameterId::compressorThresholdDb:
        return "Level above which the compressor starts reducing gain.";
    case ParameterId::compressorRatio:
        return "Amount of compression above the threshold.";
    case ParameterId::chorusMix:
        return "Wet amount of the stereo chorus.";
    case ParameterId::delayTimeMs:
        return "Echo time of the stereo delay.";
    case ParameterId::delayFeedback:
        return "How many echoes repeat.";
    case ParameterId::delayMix:
        return "Wet amount of the delay.";
    case ParameterId::reverbSize:
        return "Size of the simulated room.";
    case ParameterId::reverbDecaySeconds:
        return "Length of the reverb tail.";
    case ParameterId::reverbMix:
        return "Wet amount of the reverb.";
    case ParameterId::masterDb:
        return "Instrument output level after all effects.";
    case ParameterId::oscillatorBlend:
        return "Balance between oscillator 1 and oscillator 2.";
    case ParameterId::detuneCents:
        return "Fine tuning of oscillator 2 against oscillator 1; small values thicken.";
    case ParameterId::pitchBend:
        return "Pitch wheel; springs back to centre on release.";
    case ParameterId::modWheel:
        return "Mod wheel; scales the vibrato depth.";
    case ParameterId::oscillator2Semitones:
        return "Coarse tuning of oscillator 2 in semitones (12 = one octave).";
    case ParameterId::pulseWidth:
        return "Pulse width of square waves; 0.5 is a hollow square.";
    case ParameterId::sampleLevel:
        return "Level of the recorded sample layer (sample and hybrid sources).";
    case ParameterId::filterEnvelopeOctaves:
        return "How far the envelope sweeps the cutoff, in octaves.";
    case ParameterId::pan:
        return "Stereo position of the instrument.";
    case ParameterId::bendRangeSemitones:
        return "Pitch-wheel range in semitones.";
    case ParameterId::vibratoRateHz:
        return "Speed of the shared LFO used by vibrato and the Motion macro.";
    case ParameterId::vibratoDepthCents:
        return "Vibrato depth reached at full mod wheel.";
    case ParameterId::polyphony:
        return "Maximum notes sounding at once (poly mode).";
    case ParameterId::unisonVoices:
        return "Stacked detuned copies of every note; 1 is a single clean voice.";
    case ParameterId::unisonDetuneCents:
        return "Spread of pitch between unison copies.";
    case ParameterId::unisonSpread:
        return "Stereo width of the unison copies.";
    case ParameterId::subLevel:
        return "Sine oscillator one octave below oscillator 1.";
    case ParameterId::noiseLevel:
        return "White noise mixed before the filter (breath, air, grit).";
    case ParameterId::filterDrive:
        return "Saturation before the filter for warmth and bite.";
    case ParameterId::filterKeyTrack:
        return "Higher notes open the filter; 1 follows the keyboard fully.";
    case ParameterId::filterVelocityOctaves:
        return "Softer playing closes the filter by up to this many octaves.";
    case ParameterId::modAttackMs:
        return "Attack of the modulation envelope that drives cutoff and FM.";
    case ParameterId::modDecayMs:
        return "Decay of the modulation envelope; 0 makes cutoff/FM follow the amp envelope.";
    case ParameterId::modSustain:
        return "Sustain level of the modulation envelope.";
    case ParameterId::envelopeCurve:
        return "0 gives straight lines; 1 gives natural analogue-style curves.";
    case ParameterId::velocityToAmp:
        return "How much key velocity changes loudness.";
    case ParameterId::glideMs:
        return "Portamento time between notes.";
    case ParameterId::fmAmount:
        return "Oscillator 2 frequency-modulates oscillator 1 for bells, keys and grit.";
    case ParameterId::analogDrift:
        return "Small random pitch, phase and cutoff differences per note.";
    case ParameterId::wavetableBank:
        return "Table used by oscillators set to Wavetable: Analog Morph, Harmonic Sweep, Formant, Pulse Sweep, Digital.";
    case ParameterId::wavetablePosition:
        return "Where in the table the Wavetable oscillators play; turn it to morph the tone.";
    case ParameterId::wavetableEnvelope:
        return "How far the mod envelope (or the amp envelope when it is off) sweeps the table position.";
    default:
        return "";
    }
}
} // namespace

struct SynthEditor::Impl : private juce::FocusChangeListener
{
    struct Look final : juce::LookAndFeel_V4
    {
        Look()
        {
            setColour(juce::TextButton::buttonColourId, inset);
            setColour(juce::TextButton::buttonOnColourId, selected);
            setColour(juce::TextButton::textColourOffId, foreground.withAlpha(.85f));
            setColour(juce::TextButton::textColourOnId, accent);
            setColour(juce::ComboBox::backgroundColourId, inset);
            setColour(juce::ComboBox::textColourId, foreground);
            setColour(juce::ComboBox::outlineColourId, border);
            setColour(juce::PopupMenu::backgroundColourId, panel);
            setColour(juce::PopupMenu::textColourId, foreground);
            setColour(juce::PopupMenu::highlightedBackgroundColourId, selected);
            setColour(juce::Slider::thumbColourId, accent);
            setColour(juce::Slider::trackColourId, accent);
            setColour(juce::Slider::backgroundColourId, border);
            setColour(juce::ToggleButton::textColourId, foreground);
            setColour(juce::ToggleButton::tickColourId, accent);
            setColour(juce::TooltipWindow::backgroundColourId, panelTop);
            setColour(juce::TooltipWindow::textColourId, foreground);
            setColour(juce::TooltipWindow::outlineColourId, border);
            setColour(juce::ScrollBar::thumbColourId, border.brighter(.2f));
            setColour(juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
        }
        juce::Font getLabelFont(juce::Label &label) override
        {
            return label.getFont();
        }
        juce::Font getTextButtonFont(juce::TextButton &, int) override
        {
            return uiFont(12);
        }
        juce::Font getComboBoxFont(juce::ComboBox &) override
        {
            return uiFont(12);
        }
        juce::Label *createSliderTextBox(juce::Slider &slider) override
        {
            auto *label = LookAndFeel_V4::createSliderTextBox(slider);
            label->setFont(displayFont(13));
            return label;
        }
        void drawComboBox(juce::Graphics &g, int width, int height, bool, int, int, int, int,
                          juce::ComboBox &box) override
        {
            const auto area = juce::Rectangle<float>(0, 0, float(width), float(height)).reduced(.5f);
            g.setColour(box.isMouseOver(true) ? inset.brighter(.08f) : inset);
            g.fillRoundedRectangle(area, 4);
            g.setColour(box.hasKeyboardFocus(true) ? accent : border);
            g.drawRoundedRectangle(area, 4, 1);
            juce::Path arrow;
            const float x = float(width) - 13, y = float(height) * .5f;
            arrow.startNewSubPath(x - 4, y - 2);
            arrow.lineTo(x, y + 2);
            arrow.lineTo(x + 4, y - 2);
            g.setColour(box.isEnabled() ? muted : border);
            g.strokePath(arrow, juce::PathStrokeType(1.5f));
        }
        void positionComboBoxText(juce::ComboBox &box, juce::Label &label) override
        {
            label.setBounds(3, 0, box.getWidth() - 22, box.getHeight());
            label.setFont(getComboBoxFont(box));
        }
        void drawToggleButton(juce::Graphics &g, juce::ToggleButton &button, bool over, bool) override
        {
            // Effect power LED.
            const auto led = button.getLocalBounds().toFloat().withSizeKeepingCentre(11, 11);
            const bool on = button.getToggleState();
            if (on)
            {
                g.setColour(fxColour.withAlpha(.25f));
                g.fillEllipse(led.expanded(3));
            }
            g.setColour(on ? fxColour : inset);
            g.fillEllipse(led);
            g.setColour(on ? fxColour.brighter(.4f) : over ? muted : border.brighter(.3f));
            g.drawEllipse(led, 1.2f);
        }
        void drawLinearSlider(juce::Graphics &g, int x, int y, int width, int height, float position,
                              float minimum, float maximum, juce::Slider::SliderStyle style,
                              juce::Slider &slider) override
        {
            if (style != juce::Slider::LinearVertical)
                return LookAndFeel_V4::drawLinearSlider(g, x, y, width, height, position, minimum, maximum,
                                                        style, slider);
            // Performance wheel: recessed slot with a lit fill and a ridged thumb.
            const auto slot = juce::Rectangle<float>(float(x), float(y), float(width), float(height))
                                  .withSizeKeepingCentre(std::min(18.0f, float(width)), float(height));
            g.setColour(inset);
            g.fillRoundedRectangle(slot, 5);
            g.setColour(border);
            g.drawRoundedRectangle(slot, 5, 1);
            const auto &range = slider.getNormalisableRange();
            const bool bipolar = range.start < 0 && range.end > 0;
            const float origin = bipolar ? slot.getCentreY() : slot.getBottom();
            const auto colour = slider.findColour(juce::Slider::rotarySliderFillColourId);
            g.setColour(colour.withAlpha(.55f));
            g.fillRoundedRectangle(
                slot.withTop(std::min(origin, position)).withBottom(std::max(origin, position)).reduced(3, 0),
                3);
            const auto thumb = slot.withHeight(10).withCentre({slot.getCentreX(), position}).reduced(1, 0);
            g.setColour(foreground.withAlpha(slider.isMouseOverOrDragging() ? 1.0f : .85f));
            g.fillRoundedRectangle(thumb, 3);
            g.setColour(colour);
            g.fillRect(thumb.withSizeKeepingCentre(thumb.getWidth() - 6, 2));
        }
        juce::Font getPopupMenuFont() override
        {
            return uiFont(13);
        }
        void drawButtonBackground(juce::Graphics &g, juce::Button &button, const juce::Colour &, bool over,
                                  bool down) override
        {
            auto area = button.getLocalBounds().toFloat().reduced(.5f);
            const bool on = button.getToggleState();
            g.setColour(on ? accent.withAlpha(.16f) : inset.brighter(down ? .12f : over ? .06f : 0));
            g.fillRoundedRectangle(area, 4);
            g.setColour(on ? accent.withAlpha(.8f) : over ? border.brighter(.3f) : border);
            g.drawRoundedRectangle(area, 4, 1);
        }
        void drawRotarySlider(juce::Graphics &g, int x, int y, int width, int height, float proportion,
                              float start, float end, juce::Slider &slider) override
        {
            auto area = juce::Rectangle<float>(float(x), float(y), float(width), float(height)).reduced(3);
            const float diameter = std::min({area.getWidth(), area.getHeight(), 60.0f});
            area = area.withSizeKeepingCentre(diameter, diameter);
            const auto centre = area.getCentre();
            const float radius = diameter * .5f;
            const bool enabled = slider.isEnabled();
            const auto colour = slider.findColour(juce::Slider::rotarySliderFillColourId);
            juce::Path arc;
            arc.addCentredArc(centre.x, centre.y, radius - 2, radius - 2, 0, start, end, true);
            g.setColour(inset);
            g.strokePath(
                arc, juce::PathStrokeType(4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            // Bipolar controls (centre default) draw their arc from the middle.
            const auto &range = slider.getNormalisableRange();
            const bool bipolar = range.start < 0 && range.end > 0;
            const float origin = bipolar ? start + float(range.convertTo0to1(0)) * (end - start) : start;
            const float angle = start + proportion * (end - start);
            arc.clear();
            arc.addCentredArc(centre.x, centre.y, radius - 2, radius - 2, 0, std::min(origin, angle),
                              std::max(origin, angle), true);
            g.setColour(enabled ? colour : muted);
            g.strokePath(
                arc, juce::PathStrokeType(4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            // Machined cap lit from above, with a coloured pointer.
            const auto face = area.reduced(diameter * .17f);
            g.setColour(juce::Colours::black.withAlpha(.45f));
            g.fillEllipse(face.translated(0, 2));
            const float lift = slider.isMouseOverOrDragging() ? .1f : 0.0f;
            g.setGradientFill(juce::ColourGradient(juce::Colour(0xff3a3b47).brighter(lift), face.getCentreX(),
                                                   face.getY(), juce::Colour(0xff1a1b22), face.getCentreX(),
                                                   face.getBottom(), false));
            g.fillEllipse(face);
            g.setColour(juce::Colours::white.withAlpha(.09f));
            g.drawEllipse(face.reduced(.5f), 1);
            const float inner = face.getWidth() * .5f;
            g.setColour(enabled ? colour.brighter(.2f) : muted);
            g.drawLine(centre.x + std::sin(angle) * inner * .3f, centre.y - std::cos(angle) * inner * .3f,
                       centre.x + std::sin(angle) * inner * .85f, centre.y - std::cos(angle) * inner * .85f,
                       2.2f);
        }
    } look;
    struct Surface final : juce::Component
    {
        std::function<void(juce::Graphics &)> onPaint;
        void paint(juce::Graphics &g) override
        {
            if (onPaint)
                onPaint(g);
        }
    } content;
    struct Knob
    {
        juce::Label name;
        juce::Slider slider;
        juce::AudioProcessorParameter *parameter = nullptr;
        juce::NormalisableRange<float> range;
        ParameterId id{};
        bool gesture = false;
        void bounds(juce::Rectangle<int> area)
        {
            name.setBounds(area.removeFromBottom(16));
            slider.setBounds(area);
            if (slider.getSliderStyle() == juce::Slider::RotaryVerticalDrag)
                slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false,
                                       std::clamp(area.getWidth() - 4, 48, 96), 18);
        }
    };
    struct WorkspaceViewport final : juce::Viewport
    {
        std::function<void(juce::Rectangle<int>)> onAreaChanged;
        void visibleAreaChanged(const juce::Rectangle<int> &area) override
        {
            if (onAreaChanged)
                onAreaChanged(area);
        }
    };
    struct SynthKeyboard final : juce::MidiKeyboardComponent
    {
        juce::MidiKeyboardState &state;
        SynthKeyboard(juce::MidiKeyboardState &s, Orientation orientation)
            : MidiKeyboardComponent(s, orientation), state(s)
        {
        }
        void releaseHeldInput()
        {
            // The pinned Windows peer delivers capture loss as mouseUp. Other
            // editor cleanup paths must also clear JUCE's private held-key
            // bookkeeping, so a later press on the same note is recognised.
            juce::MidiKeyboardComponent::focusLost(focusChangedDirectly);
            state.allNotesOff(0);
        }
        // Keys held by host/external MIDI (a hardware keyboard routed by the host).
        std::array<std::uint64_t, 2> external{};
        bool externallyHeld(int note) const
        {
            return note >= 0 && note < 128 && ((external[size_t(note >> 6)] >> (note & 63)) & 1) != 0;
        }
        void drawWhiteNote(int note, juce::Graphics &g, juce::Rectangle<float> area, bool isDown, bool isOver,
                           juce::Colour, juce::Colour) override
        {
            const bool down = isDown || externallyHeld(note);
            const auto key = area.reduced(.5f, 0);
            g.setGradientFill(juce::ColourGradient(down ? accent.brighter(.35f) : juce::Colour(0xfff7f4ef), 0,
                                                   key.getY(), down ? accent : juce::Colour(0xffd6d2ca), 0,
                                                   key.getBottom(), false));
            g.fillRect(key);
            if (isOver && !down)
            {
                g.setColour(accent.withAlpha(.14f));
                g.fillRect(key);
            }
            g.setColour(juce::Colour(0xff8f8b84));
            g.fillRect(key.withX(key.getRight() - .5f).withWidth(1));
            g.setColour(juce::Colours::black.withAlpha(.12f));
            g.fillRect(key.withTop(key.getBottom() - 3));
            if (note % 12 == 0)
            {
                g.setColour(down ? juce::Colours::black.withAlpha(.6f) : juce::Colour(0xff8a867f));
                g.setFont(uiFont(10));
                g.drawText(getWhiteNoteText(note), key.withTrimmedBottom(5).toNearestInt(),
                           juce::Justification::centredBottom);
            }
        }
        void drawBlackNote(int note, juce::Graphics &g, juce::Rectangle<float> area, bool isDown, bool isOver,
                           juce::Colour) override
        {
            const bool down = isDown || externallyHeld(note);
            const auto key = area.reduced(1, 0).withTrimmedBottom(1);
            g.setGradientFill(juce::ColourGradient(
                down ? accent.darker(.15f) : juce::Colour(0xff34353f), 0, key.getY(),
                down ? accent.darker(.6f) : juce::Colour(0xff0d0e12), 0, key.getBottom(), false));
            g.fillRoundedRectangle(key, 2);
            // Lighter front lip gives the key depth.
            g.setColour(juce::Colours::white.withAlpha(down ? .25f : isOver ? .2f : .1f));
            g.fillRoundedRectangle(key.withTop(key.getBottom() - 5).reduced(2, 1), 1.5f);
        }
    };
    struct PresetModel final : juce::ListBoxModel
    {
        Impl &owner;
        explicit PresetModel(Impl &o) : owner(o) {}
        int getNumRows() override
        {
            return int(owner.visible.size());
        }
        void paintListBoxItem(int row, juce::Graphics &g, int width, int height, bool isSelected) override
        {
            if (row < 0 || size_t(row) >= owner.visible.size())
                return;
            const auto &preset = factoryPresets()[owner.visible[size_t(row)]];
            auto area = juce::Rectangle<int>(2, 1, width - 4, height - 2).toFloat();
            if (isSelected)
            {
                g.setColour(accent.withAlpha(.14f));
                g.fillRoundedRectangle(area, 5);
                g.setColour(accent);
                g.fillRoundedRectangle(area.withWidth(3).reduced(0, 6), 1.5f);
            }
            else if (row % 2 == 1)
            {
                g.setColour(juce::Colours::white.withAlpha(.025f));
                g.fillRoundedRectangle(area, 5);
            }
            g.setColour(owner.favourite(preset.id) ? accent : muted.withAlpha(.6f));
            g.setFont(juce::FontOptions(17));
            g.drawText(star(owner.favourite(preset.id)), 6, 0, 26, height, juce::Justification::centred);
            g.setColour(isSelected ? accent.brighter(.3f) : foreground);
            g.setFont(uiFont(13, true));
            g.drawText(preset.name, 36, 4, width - 44, 18, juce::Justification::centredLeft);
            g.setColour(muted);
            g.setFont(uiFont(11));
            const auto dot = "  " + juce::String::charToString(0xb7) + "  ";
            g.drawText(juce::String(preset.family) + dot + juce::String(preset.genre).replace("|", ", ") +
                           dot + juce::String(sourceKindName(preset.source)).toLowerCase(),
                       36, 21, width - 44, 15, juce::Justification::centredLeft);
        }
        void returnKeyPressed(int row) override
        {
            if (row >= 0 && size_t(row) < owner.visible.size())
                owner.selectPreset(owner.visible[size_t(row)]);
        }
        void selectedRowsChanged(int row) override
        {
            if (owner.synchronizing)
                return;
            owner.browser.selectedPresetId = row >= 0 && size_t(row) < owner.visible.size()
                                                 ? factoryPresets()[owner.visible[size_t(row)]].id
                                                 : juce::String{};
            owner.persistBrowser();
        }
        void listBoxItemClicked(int row, const juce::MouseEvent &event) override
        {
            if (event.x < 36 && row >= 0 && size_t(row) < owner.visible.size())
                owner.toggleFavourite(factoryPresets()[owner.visible[size_t(row)]].id);
            else if (row >= 0 && size_t(row) < owner.visible.size())
                owner.selectPreset(owner.visible[size_t(row)]);
        }
    } presetModel;

    SynthEditor &editor;
    juce::AudioProcessor &processor;
    Callbacks callbacks;
    WorkspaceViewport viewport;
    SynthKeyboard keyboard;
    juce::ListBox presetList{"Synth factory presets", &presetModel};
    juce::TextEditor search;
    juce::ComboBox family, filterType, sourceType, oscillator1, oscillator2, filterModel, voiceMode;
    juce::Label brand, heading, presetHeading, presetDescription, sourceBadge, emptyPresets, status,
        voiceCount, octaveLabel;
    juce::TextButton previous{"<"}, next{">"}, favouriteButton{star(false)}, save{"Save preset..."},
        load{"Load preset..."}, octaveDown{"-"}, octaveUp{"+"};
    std::vector<std::unique_ptr<juce::TextButton>> genres;
    std::array<std::unique_ptr<Knob>, parameterCount> knobs;
    std::array<juce::ToggleButton, 5> effectEnabled;
    std::array<juce::TextButton, 5> effectTabs;
    std::vector<std::size_t> visible;
    std::unique_ptr<juce::FileChooser> chooser;
    SynthBrowserState browser;
    juce::String currentId, localError, lastStatus;
    bool synchronizing = false;
    int activeEffect = 0, octave = 0, browserPoll = 0;
    float peakLeft = -1, peakRight = -1;
    juce::Rectangle<int> headerArea, displayArea, meterArea, footerArea, browserArea, macroArea,
        oscillatorArea, unisonArea, filterArea, envelopeArea, modulationArea, voiceArea, effectsArea;
    std::array<float, 8> paintedShape{};
    static constexpr std::array<ParameterId, 5> enabledIds{
        ParameterId::eqEnabled, ParameterId::compressorEnabled, ParameterId::chorusEnabled,
        ParameterId::delayEnabled, ParameterId::reverbEnabled};
    static constexpr std::array<const char *, 5> effectNames{"EQ", "Compressor", "Chorus", "Delay", "Reverb"};
    static constexpr std::array<const char *, 5> effectLabels{"EQ", "Comp", "Chorus", "Delay", "Reverb"};

    Impl(SynthEditor &owner, juce::AudioProcessor &p, juce::MidiKeyboardState &state, Callbacks c)
        : presetModel(*this), editor(owner), processor(p), callbacks(std::move(c)),
          keyboard(state, juce::MidiKeyboardComponent::horizontalKeyboard)
    {
        editor.setLookAndFeel(&look);
        editor.setComponentID("synth.editor");
        editor.setName("VibeRa Synthesizer");
        for (auto *component : std::initializer_list<juce::Component *>{
                 &viewport, &keyboard, &brand, &heading, &previous, &next, &favouriteButton, &save, &load,
                 &presetHeading, &sourceBadge, &status, &voiceCount, &octaveLabel, &octaveDown, &octaveUp})
            editor.addAndMakeVisible(component);
        viewport.setViewedComponent(&content, false);
        // Scroll bars appear only when the window is smaller than the module grid.
        viewport.setScrollBarsShown(true, true);
        viewport.setScrollBarThickness(9);
        viewport.setComponentID("synth.workspaceScroll");
        content.onPaint = [this](juce::Graphics &g) { paintContent(g); };
        for (auto *component : std::initializer_list<juce::Component *>{
                 &search, &family, &presetList, &emptyPresets, &presetDescription, &filterType, &sourceType,
                 &oscillator1, &oscillator2, &filterModel, &voiceMode})
            content.addAndMakeVisible(component);
        brand.setText("NATIVE INSTRUMENT", juce::dontSendNotification);
        brand.setFont(displayFont(10).withKerningFactor(.18f));
        brand.setColour(juce::Label::textColourId, muted);
        heading.setText("VIBERA SYNTH", juce::dontSendNotification);
        heading.setFont(displayFont(20, true).withKerningFactor(.06f));
        heading.setColour(juce::Label::textColourId, foreground);
        presetHeading.setFont(displayFont(19, true));
        presetHeading.setColour(juce::Label::textColourId, foreground);
        presetHeading.setJustificationType(juce::Justification::centred);
        sourceBadge.setFont(displayFont(11).withKerningFactor(.08f));
        sourceBadge.setColour(juce::Label::textColourId, accent);
        sourceBadge.setJustificationType(juce::Justification::centred);
        styleLabel(presetDescription, 12, muted);
        styleLabel(status, 11, muted);
        styleLabel(voiceCount, 11, muted);
        octaveLabel.setFont(displayFont(13));
        octaveLabel.setColour(juce::Label::textColourId, foreground);
        octaveLabel.setJustificationType(juce::Justification::centred);
        presetHeading.setTooltip(
            "Current sound. An asterisk means you changed it from its factory settings.");
        sourceBadge.setTooltip("Sound source: synthesis, a recorded sample map, or both layered.");
        voiceCount.setTooltip("Notes sounding now / configured polyphony.");
        presetDescription.setMinimumHorizontalScale(1.0f);
        presetDescription.setJustificationType(juce::Justification::topLeft);
        previous.setComponentID("synth.preset.previous");
        next.setComponentID("synth.preset.next");
        favouriteButton.setComponentID("synth.preset.favourite");
        save.setComponentID("synth.preset.save");
        load.setComponentID("synth.preset.load");
        previous.setTooltip("Load the previous preset in the current filtered list.");
        next.setTooltip("Load the next preset in the current filtered list.");
        favouriteButton.setTooltip("Add or remove the current preset from Favourites.");
        save.setTooltip("Save the current sound as a .vrsynth user preset.");
        load.setTooltip("Load a .vrsynth user preset from the Synthesizer user folder.");
        octaveDown.setTooltip("Shift the on-screen keyboard down one octave.");
        octaveUp.setTooltip("Shift the on-screen keyboard up one octave.");
        previous.onClick = [this] { stepPreset(-1); };
        next.onClick = [this] { stepPreset(1); };
        favouriteButton.onClick = [this]
        {
            if (currentId.isNotEmpty())
                toggleFavourite(currentId);
        };
        save.setEnabled(bool(callbacks.saveUserPreset));
        load.setEnabled(bool(callbacks.loadUserPreset));
        save.onClick = [this] { choosePresetFile(true); };
        load.onClick = [this] { choosePresetFile(false); };
        search.setComponentID("synth.browser.search");
        search.setTooltip("Search preset names, source descriptions and tags within the selected genre and "
                          "instrument family.");
        search.setTextToShowWhenEmpty("Search presets, instruments or sources", muted);
        search.setColour(juce::TextEditor::backgroundColourId, inset);
        search.setColour(juce::TextEditor::textColourId, foreground);
        search.setColour(juce::TextEditor::outlineColourId, border);
        search.onTextChange = [this]
        {
            if (synchronizing)
                return;
            browser.search = search.getText();
            persistBrowser();
            rebuildList();
        };
        family.setComponentID("synth.browser.family");
        family.setTooltip("Filter instrument families within the current genre, search and favourites.");
        family.addItem("All instruments", 1);
        for (const auto *name : familyNames())
            family.addItem(name, family.getNumItems() + 1);
        family.onChange = [this]
        {
            if (synchronizing)
                return;
            browser.family = family.getSelectedId() == 1 ? "All" : family.getText();
            persistBrowser();
            rebuildList();
        };
        presetList.setComponentID("synth.browser.presets");
        presetList.setRowHeight(40);
        presetList.setTooltip(
            "Click a preset to load. Arrow keys select; Enter loads. The star changes only favourites.");
        presetList.setColour(juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        emptyPresets.setComponentID("synth.browser.empty");
        emptyPresets.setName("No presets match");
        emptyPresets.setText("No presets match.\nClear Search, then choose All Genres and All instruments.\n"
                             "Turn off Favourites if it is selected.",
                             juce::dontSendNotification);
        styleLabel(emptyPresets, 12, muted);
        emptyPresets.setMinimumHorizontalScale(1.0f);
        emptyPresets.setJustificationType(juce::Justification::topLeft);
        emptyPresets.setInterceptsMouseClicks(false, false);
        const auto addGenre = [&](const juce::String &name, bool favourites)
        {
            auto button = std::make_unique<juce::TextButton>(name);
            button->setComponentID("synth.genre." + name);
            button->setTooltip(favourites             ? juce::String("Show only your favourite presets.")
                               : name == "All Genres" ? juce::String("Show presets from every genre.")
                                                      : "Show presets tagged " + name + ".");
            button->onClick = [this, name, favourites]
            {
                browser.favouritesOnly = favourites;
                browser.genre = favourites || name == "All Genres" ? "All Genres" : name;
                persistBrowser();
                synchronizeBrowser();
            };
            content.addAndMakeVisible(*button);
            genres.push_back(std::move(button));
        };
        addGenre("All Genres", false);
        addGenre("Favourites", true);
        for (const auto *name : genreNames())
            addGenre(name, false);

        for (const auto &spec : parameterSpecs())
        {
            if (spec.id == ParameterId::factoryPreset || spec.id == ParameterId::filterType ||
                spec.id == ParameterId::sourceType || spec.id == ParameterId::oscillatorType ||
                spec.id == ParameterId::oscillator2Type || spec.id == ParameterId::filterModel ||
                spec.id == ParameterId::voiceMode ||
                std::find(enabledIds.begin(), enabledIds.end(), spec.id) != enabledIds.end())
                continue;
            auto knob = std::make_unique<Knob>();
            knob->id = spec.id;
            knob->parameter = parameter(spec.id);
            knob->range = {spec.minimum, spec.maximum, spec.interval, spec.skew};
            knob->name.setText(spec.name, juce::dontSendNotification);
            knob->name.setJustificationType(juce::Justification::centred);
            styleLabel(knob->name, 11, muted);
            knob->name.setMinimumHorizontalScale(.7f);
            knob->name.setInterceptsMouseClicks(false, false);
            auto &slider = knob->slider;
            slider.setComponentID("synth.parameter." + juce::String(spec.key));
            slider.setName(spec.name);
            slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
            slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 84, 18);
            slider.setColour(juce::Slider::rotarySliderFillColourId, moduleColour(spec.id));
            slider.setColour(juce::Slider::textBoxTextColourId, foreground);
            slider.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
            slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
            slider.setColour(juce::Slider::textBoxHighlightColourId, accent.withAlpha(.35f));
            slider.setNormalisableRange(
                juce::NormalisableRange<double>(spec.minimum, spec.maximum, spec.interval, spec.skew));
            const auto shortUnit = juce::String(spec.unit);
            slider.setNumDecimalPlacesToDisplay(spec.interval > 0 ? 0
                                                : shortUnit == "Hz" || shortUnit == "ms" ||
                                                        shortUnit == "cents" || shortUnit == "dB" ||
                                                        shortUnit == "oct"
                                                    ? 1
                                                    : 2);
            if (juce::String(spec.unit).isNotEmpty())
                slider.setTextValueSuffix(" " + juce::String(spec.unit));
            slider.setDoubleClickReturnValue(true, spec.initial);
            const auto unit = juce::String(spec.unit);
            slider.setTooltip(
                juce::String(spec.name) + ": " + parameterHelp(spec.id) + "\nDouble-click restores " +
                juce::String(spec.initial, spec.interval > 0 ? 0 : 2) +
                (unit.isNotEmpty() ? " " + unit : juce::String()) + ". Edits share the host Undo history.");
            slider.setEnabled(knob->parameter != nullptr);
            auto *binding = knob.get();
            slider.onDragStart = [binding]
            {
                if (binding->parameter && !binding->gesture)
                {
                    binding->parameter->beginChangeGesture();
                    binding->gesture = true;
                }
            };
            slider.onValueChange = [binding]
            {
                if (!binding->parameter)
                    return;
                const bool single = !binding->gesture;
                if (single)
                    binding->parameter->beginChangeGesture();
                binding->parameter->setValueNotifyingHost(
                    binding->range.convertTo0to1(float(binding->slider.getValue())));
                if (single)
                    binding->parameter->endChangeGesture();
            };
            slider.onDragEnd = [binding]
            {
                if (binding->id == ParameterId::pitchBend)
                    binding->slider.setValue(0, juce::sendNotificationSync);
                if (binding->parameter && binding->gesture)
                {
                    binding->parameter->endChangeGesture();
                    binding->gesture = false;
                }
            };
            content.addAndMakeVisible(knob->name);
            content.addAndMakeVisible(slider);
            knobs[parameterIndex(spec.id)] = std::move(knob);
        }
        for (const auto id : {ParameterId::masterDb, ParameterId::pitchBend, ParameterId::modWheel})
        {
            auto &knob = *knobs[parameterIndex(id)];
            editor.addAndMakeVisible(knob.name);
            editor.addAndMakeVisible(knob.slider);
            if (id == ParameterId::masterDb)
            {
                knob.name.setText("MASTER", juce::dontSendNotification);
                knob.name.setFont(displayFont(10).withKerningFactor(.15f));
                knob.name.setJustificationType(juce::Justification::centredRight);
                knob.slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 72, 20);
            }
            else
            {
                knob.name.setText(id == ParameterId::pitchBend ? "Pitch" : "Mod", juce::dontSendNotification);
                knob.name.setTooltip(parameterSpec(id).name);
                knob.slider.setSliderStyle(juce::Slider::LinearVertical);
                knob.slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
            }
        }
        bindCombo(filterType, ParameterId::filterType, {"Low Pass", "High Pass", "Band Pass", "Bypass"},
                  "Filter response; Bypass sends the oscillators straight to the amplifier.");
        bindCombo(sourceType, ParameterId::sourceType, {"Oscillators", "Recorded sample", "Sample + synth"},
                  "Sound source; recorded options need a preset with a sample map.");
        bindCombo(oscillator1, ParameterId::oscillatorType, {"Sine", "Saw", "Square", "Triangle", "Noise", "Wavetable"},
                  "Waveform of oscillator 1 (the FM carrier).");
        bindCombo(oscillator2, ParameterId::oscillator2Type, {"Sine", "Saw", "Square", "Triangle", "Noise", "Wavetable"},
                  "Waveform of oscillator 2 (also the FM modulator).");
        bindCombo(filterModel, ParameterId::filterModel, {"12 dB SVF", "24 dB Ladder"},
                  "Filter slope: smooth 12 dB state-variable or a steeper, saturating 24 dB ladder.");
        bindCombo(voiceMode, ParameterId::voiceMode, {"Poly", "Mono", "Legato"},
                  "Poly plays chords; Mono retriggers one voice; Legato glides without retriggering.");
        for (std::size_t index = 0; index < effectTabs.size(); ++index)
        {
            auto &enabled = effectEnabled[index];
            enabled.setButtonText({});
            enabled.setComponentID("synth.parameter." + juce::String(parameterSpec(enabledIds[index]).key));
            enabled.setTooltip("Turn the " + juce::String(effectNames[index]) +
                               " on or off; its settings are kept.");
            effectTabs[index].setTooltip("Show the " + juce::String(effectNames[index]) + " settings.");
            enabled.onClick = [this, index]
            { setNative(enabledIds[index], effectEnabled[index].getToggleState() ? 1 : 0); };
            effectTabs[index].setButtonText(effectLabels[index]);
            effectTabs[index].setComponentID("synth.effects." + juce::String(effectNames[index]));
            effectTabs[index].onClick = [this, index]
            {
                browser.effectPage = int(index);
                persistBrowser();
                layout();
            };
            content.addAndMakeVisible(enabled);
            content.addAndMakeVisible(effectTabs[index]);
        }
        keyboard.setComponentID("synth.keyboard");
        keyboard.setName("Playable synthesizer keyboard");
        keyboard.setAvailableRange(24, 108);
        keyboard.setScrollButtonsVisible(false);
        keyboard.setLowestVisibleKey(36);
        keyboard.setKeyWidth(23);
        // White keys paint themselves; this colour fills only the deck beyond the last key.
        keyboard.setColour(juce::MidiKeyboardComponent::whiteNoteColourId, juce::Colour(0xff0b0c0f));
        keyboard.setColour(juce::MidiKeyboardComponent::blackNoteColourId, juce::Colour(0xff13151a));
        keyboard.setColour(juce::MidiKeyboardComponent::keyDownOverlayColourId, accent.withAlpha(.65f));
        keyboard.setColour(juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, accent.withAlpha(.2f));
        keyboard.setColour(juce::MidiKeyboardComponent::shadowColourId, juce::Colours::black.withAlpha(.45f));
        keyboard.setColour(juce::MidiKeyboardComponent::keySeparatorLineColourId, juce::Colour(0xff8f8b84));
        octaveDown.setComponentID("synth.keyboard.octaveDown");
        octaveUp.setComponentID("synth.keyboard.octaveUp");
        octaveDown.onClick = [this] { changeOctave(-1); };
        octaveUp.onClick = [this] { changeOctave(1); };
        if (callbacks.browserState)
            browser = callbacks.browserState();
        viewport.onAreaChanged = [this](juce::Rectangle<int> area)
        {
            if (synchronizing || editor.getWidth() <= 0)
                return;
            browser.scrollX = std::clamp(area.getX(), 0, 10000);
            browser.scrollY = std::clamp(area.getY(), 0, 10000);
            persistBrowser();
        };
        synchronizeBrowser();
        refreshParameters();
        juce::Desktop::getInstance().addFocusChangeListener(this);
    }
    ~Impl() override
    {
        juce::Desktop::getInstance().removeFocusChangeListener(this);
        releaseInput();
        for (auto &knob : knobs)
            if (knob && knob->gesture && knob->parameter)
                knob->parameter->endChangeGesture();
        viewport.setViewedComponent(nullptr, false);
        editor.setLookAndFeel(nullptr);
    }
    juce::AudioProcessorParameter *parameter(ParameterId id) const
    {
        const auto index = parameterIndex(id);
        return index < size_t(processor.getParameters().size()) ? processor.getParameters()[int(index)]
                                                                : nullptr;
    }
    float nativeValue(ParameterId id) const
    {
        const auto &spec = parameterSpec(id);
        auto *p = parameter(id);
        return p ? juce::NormalisableRange<float>(spec.minimum, spec.maximum, spec.interval, spec.skew)
                       .convertFrom0to1(p->getValue())
                 : spec.initial;
    }
    void setNative(ParameterId id, float value)
    {
        if (auto *p = parameter(id))
        {
            const auto &spec = parameterSpec(id);
            p->beginChangeGesture();
            p->setValueNotifyingHost(
                juce::NormalisableRange<float>(spec.minimum, spec.maximum, spec.interval, spec.skew)
                    .convertTo0to1(value));
            p->endChangeGesture();
        }
    }
    void bindCombo(juce::ComboBox &combo, ParameterId id, std::initializer_list<const char *> choices,
                   const char *help)
    {
        combo.setComponentID("synth.parameter." + juce::String(parameterSpec(id).key));
        combo.setName(parameterSpec(id).name);
        combo.setTooltip(juce::String(parameterSpec(id).name) + ": " + help +
                         " Saved with the sound and Undo history.");
        for (const auto *choice : choices)
            combo.addItem(choice, combo.getNumItems() + 1);
        combo.setEnabled(parameter(id) != nullptr);
        combo.onChange = [this, &combo, id]
        {
            if (!synchronizing && combo.getSelectedId() > 0)
                setNative(id, float(combo.getSelectedId() - 1));
        };
    }
    bool favourite(const juce::String &id) const
    {
        return browser.favouriteIds.contains(id);
    }
    void persistBrowser()
    {
        if (callbacks.setBrowserState)
        {
            const auto result = callbacks.setBrowserState(browser);
            localError = result.failed() ? result.getErrorMessage() : juce::String{};
        }
    }
    void synchronizeBrowser()
    {
        synchronizing = true;
        if (search.getText() != browser.search)
        {
            const auto selection = search.getHighlightedRegion();
            const int caret = search.getCaretPosition();
            search.setText(browser.search, false);
            if (!selection.isEmpty())
                search.setHighlightedRegion({std::min(selection.getStart(), browser.search.length()),
                                             std::min(selection.getEnd(), browser.search.length())});
            else
                search.setCaretPosition(std::min(caret, browser.search.length()));
        }
        int familyId = 1;
        for (int index = 1; index < family.getNumItems(); ++index)
            if (family.getItemText(index) == browser.family)
                familyId = index + 1;
        family.setSelectedId(familyId, juce::dontSendNotification);
        const bool favourites =
            browser.favouritesOnly || browser.genre == "Favorites" || browser.genre == "Favourites";
        for (auto &genre : genres)
            genre->setToggleState(favourites ? genre->getButtonText() == "Favourites"
                                             : genre->getButtonText() == browser.genre,
                                  juce::dontSendNotification);
        synchronizing = false;
        rebuildList();
        if (editor.getWidth() > 0)
            layout();
    }
    void rebuildList()
    {
        visible.clear();
        const auto presets = factoryPresets();
        for (std::size_t index = 0; index < presets.size(); ++index)
        {
            const auto &p = presets[index];
            if (!synthPresetMatchesBrowser(p, browser))
                continue;
            visible.push_back(index);
        }
        synchronizing = true;
        presetList.updateContent();
        presetList.deselectAllRows();
        for (size_t row = 0; row < visible.size(); ++row)
            if (browser.selectedPresetId == presets[visible[row]].id)
            {
                presetList.selectRow(int(row));
                break;
            }
        synchronizing = false;
        previous.setEnabled(!visible.empty());
        next.setEnabled(!visible.empty());
        emptyPresets.setVisible(visible.empty());
        presetList.repaint();
    }
    void toggleFavourite(const juce::String &id)
    {
        if (favourite(id))
            browser.favouriteIds.removeString(id);
        else
            browser.favouriteIds.addIfNotAlreadyThere(id);
        persistBrowser();
        rebuildList();
        favouriteButton.setButtonText(star(favourite(currentId)));
    }
    void selectPreset(std::size_t index)
    {
        if (index >= factoryPresets().size())
            return;
        releaseInput();
        const auto &preset = factoryPresets()[index];
        juce::Result result = juce::Result::ok();
        if (callbacks.selectFactoryPreset)
            result = callbacks.selectFactoryPreset(preset.id);
        else
        {
            bool complete = true;
            for (const auto &spec : parameterSpecs())
                complete = complete && parameter(spec.id) != nullptr;
            if (!complete)
                result = juce::Result::fail("Complete native patch controls are unavailable");
            else
            {
                for (const auto &spec : parameterSpecs())
                    parameter(spec.id)->beginChangeGesture();
                for (const auto &spec : parameterSpecs())
                    parameter(spec.id)->setValueNotifyingHost(
                        juce::NormalisableRange<float>(spec.minimum, spec.maximum, spec.interval, spec.skew)
                            .convertTo0to1(preset.values[parameterIndex(spec.id)]));
                for (const auto &spec : parameterSpecs())
                    parameter(spec.id)->endChangeGesture();
            }
        }
        if (result.failed())
            localError = result.getErrorMessage();
        else
        {
            localError.clear();
            lastStatus.clear();
            currentId = preset.id;
            browser.selectedPresetId = preset.id;
            persistBrowser();
        }
        refreshParameters();
    }
    void stepPreset(int direction)
    {
        const auto id = adjacentSynthPresetId(browser, currentId, direction);
        const auto *preset = findFactoryPreset(id.toStdString());
        if (!preset)
            return;
        selectPreset(std::size_t(preset - factoryPresets().data()));
        rebuildList();
    }
    void choosePresetFile(bool saving)
    {
        if ((saving && !callbacks.saveUserPreset) || (!saving && !callbacks.loadUserPreset))
        {
            localError = "User preset service is unavailable";
            return;
        }
        const auto directory = synthUserDirectory();
        if (saving && directory.createDirectory().failed())
        {
            localError = "Cannot create the user preset directory";
            return;
        }
        const auto start = saving ? directory.getChildFile("My Synth Preset.vrsynth") : directory;
        chooser = std::make_unique<juce::FileChooser>(
            saving ? "Save synthesizer preset" : "Load synthesizer preset", start, "*.vrsynth", true);
        juce::Component::SafePointer<SynthEditor> safe(&editor);
        chooser->launchAsync(
            (saving ? juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting
                    : juce::FileBrowserComponent::openMode) |
                juce::FileBrowserComponent::canSelectFiles,
            [safe, saving](const juce::FileChooser &dialog)
            {
                if (!safe || dialog.getResult() == juce::File())
                    return;
                auto &self = *safe->impl_;
                auto file = dialog.getResult();
                if (saving && file.getFileExtension().isEmpty())
                    file = file.withFileExtension(".vrsynth");
                const auto name = file.getFileNameWithoutExtension();
                if (saving &&
                    (file.getParentDirectory() != synthUserDirectory() || !file.hasFileExtension("vrsynth") ||
                     name.isEmpty() || name.length() > 100 || name.containsAnyOf("/\\:<>|?*\".")))
                {
                    self.localError = "Save in the Synthesizer user directory with a plain name up to 100 "
                                      "characters and .vrsynth extension";
                    self.refreshParameters();
                    return;
                }
                const auto managedDirectory = synthUserDirectory();
                bool managedLoad = file.isAChildOf(managedDirectory);
                if (!saving && managedLoad)
                    for (auto path = file; path != managedDirectory; path = path.getParentDirectory())
                        if (path.isSymbolicLink())
                        {
                            managedLoad = false;
                            break;
                        }
                if (!saving && !managedLoad)
                {
                    self.localError = "Load a preset from the managed Synthesizer user directory";
                    self.refreshParameters();
                    return;
                }
                const auto result =
                    saving ? self.callbacks.saveUserPreset(file) : self.callbacks.loadUserPreset(file);
                self.localError = result.failed() ? result.getErrorMessage() : juce::String{};
                self.lastStatus = result.wasOk() ? saving ? "User preset saved" : "User preset loaded" : "";
                self.refreshParameters();
            });
    }
    void changeOctave(int direction)
    {
        browser.octave = std::clamp(browser.octave + direction, -1, 4);
        persistBrowser();
        layout();
    }
    void releaseInput()
    {
        keyboard.releaseHeldInput();
        auto &pitch = knobs[parameterIndex(ParameterId::pitchBend)];
        if (pitch && pitch->gesture)
            pitch->slider.setValue(0, juce::sendNotificationSync);
        else if (std::abs(nativeValue(ParameterId::pitchBend)) > .00001f)
            setNative(ParameterId::pitchBend, 0);
        for (auto &knob : knobs)
            if (knob && knob->gesture && knob->parameter)
            {
                knob->parameter->endChangeGesture();
                knob->gesture = false;
            }
    }
    void globalFocusChanged(juce::Component *focused) override
    {
        if (!focused || (focused != &editor && !editor.isParentOf(focused)))
            releaseInput();
    }
    void refreshParameters()
    {
        for (auto &knob : knobs)
            if (knob && knob->parameter && !knob->gesture && !knob->slider.isMouseButtonDown())
                knob->slider.setValue(knob->range.convertFrom0to1(knob->parameter->getValue()),
                                      juce::dontSendNotification);
        synchronizing = true;
        filterType.setSelectedId(int(std::lround(nativeValue(ParameterId::filterType))) + 1,
                                 juce::dontSendNotification);
        sourceType.setSelectedId(int(std::lround(nativeValue(ParameterId::sourceType))) + 1,
                                 juce::dontSendNotification);
        oscillator1.setSelectedId(int(std::lround(nativeValue(ParameterId::oscillatorType))) + 1,
                                  juce::dontSendNotification);
        oscillator2.setSelectedId(int(std::lround(nativeValue(ParameterId::oscillator2Type))) + 1,
                                  juce::dontSendNotification);
        filterModel.setSelectedId(int(std::lround(nativeValue(ParameterId::filterModel))) + 1,
                                  juce::dontSendNotification);
        voiceMode.setSelectedId(int(std::lround(nativeValue(ParameterId::voiceMode))) + 1,
                                juce::dontSendNotification);
        for (size_t index = 0; index < effectEnabled.size(); ++index)
            effectEnabled[index].setToggleState(nativeValue(enabledIds[index]) >= .5f,
                                                juce::dontSendNotification);
        synchronizing = false;
        const auto priorId = currentId;
        if (callbacks.currentPresetId)
            currentId = callbacks.currentPresetId();
        else
        {
            const auto index = size_t(std::clamp(int(std::lround(nativeValue(ParameterId::factoryPreset))), 0,
                                                 int(factoryPresets().size()) - 1));
            currentId = factoryPresets()[index].id;
        }
        const auto *preset = findFactoryPreset(currentId.toStdString());
        bool modified = false;
        if (preset)
            for (const auto &spec : parameterSpecs())
                if (spec.id != ParameterId::factoryPreset && spec.id != ParameterId::pitchBend &&
                    spec.id != ParameterId::modWheel &&
                    std::abs(nativeValue(spec.id) - preset->values[parameterIndex(spec.id)]) >
                        std::max(.0001f, (spec.maximum - spec.minimum) * .00001f))
                    modified = true;
        presetHeading.setText((preset ? juce::String(preset->name) : juce::String("User Preset")) +
                                  (modified ? " *" : ""),
                              juce::dontSendNotification);
        presetDescription.setText(preset ? preset->description : "Your saved instrument settings.",
                                  juce::dontSendNotification);
        const bool hasSample = preset && juce::String(preset->sampleAsset).isNotEmpty();
        sourceType.setItemEnabled(2, hasSample);
        sourceType.setItemEnabled(3, hasSample);
        const auto actualSource =
            static_cast<SourceKind>(std::clamp(int(std::lround(nativeValue(ParameterId::sourceType))), 0, 2));
        sourceBadge.setText(
            juce::String(sourceKindName(actualSource)) +
                (actualSource != SourceKind::synthesis && !hasSample ? " / NO SAMPLE MAP" : "") +
                (modified ? "   MODIFIED" : ""),
            juce::dontSendNotification);
        favouriteButton.setEnabled(preset != nullptr);
        favouriteButton.setButtonText(star(favourite(currentId)));
        if (priorId != currentId)
        {
            browser.selectedPresetId = currentId;
            persistBrowser();
            rebuildList();
        }
        if (callbacks.activeVoiceCount)
            voiceCount.setText("Voices  " + juce::String(callbacks.activeVoiceCount()) + " / " +
                                   juce::String(int(nativeValue(ParameterId::polyphony))),
                               juce::dontSendNotification);
        if (callbacks.peak)
        {
            peakLeft = callbacks.peak(0);
            peakRight = callbacks.peak(1);
        }
        juce::String message = localError.isNotEmpty()   ? localError
                               : lastStatus.isNotEmpty() ? lastStatus
                               : callbacks.loadStatus    ? callbacks.loadStatus()
                                                         : juce::String{};
        if (message.isEmpty())
            message = "Factory presets: " + juce::String(int(factoryPresets().size())) + "   /   " +
                      juce::String(int(visible.size())) + " shown   /   Parameter edits share host Undo";
        status.setText(message, juce::dontSendNotification);
        status.setColour(juce::Label::textColourId,
                         localError.isNotEmpty() ? juce::Colour(0xffffac98) : muted);
        // Envelope plots are the only painted values; repaint them only on change.
        const std::array<float, 8> shape{
            nativeValue(ParameterId::attackMs),      nativeValue(ParameterId::decayMs),
            nativeValue(ParameterId::sustain),       nativeValue(ParameterId::releaseMs),
            nativeValue(ParameterId::envelopeCurve), nativeValue(ParameterId::modAttackMs),
            nativeValue(ParameterId::modDecayMs),    nativeValue(ParameterId::modSustain)};
        if (shape != paintedShape)
        {
            paintedShape = shape;
            content.repaint(envelopeArea.withHeight(cardHeader));
            content.repaint(modulationArea.withHeight(cardHeader));
        }
        editor.repaint(meterArea.expanded(2));
    }
    void tick()
    {
        if (callbacks.browserState && ++browserPoll >= 5)
        {
            browserPoll = 0;
            auto updated = callbacks.browserState();
            if (!sameBrowser(updated, browser))
            {
                browser = updated;
                synchronizeBrowser();
            }
        }
        if (callbacks.externalNoteHeld)
        {
            std::array<std::uint64_t, 2> held{};
            for (int note = 0; note < 128; ++note)
                if (callbacks.externalNoteHeld(note))
                    held[size_t(note >> 6)] |= std::uint64_t(1) << (note & 63);
            if (held != keyboard.external)
            {
                keyboard.external = held;
                keyboard.repaint();
            }
        }
        refreshParameters();
        synchronizePresetScroll(false);
    }
    void synchronizePresetScroll(bool adopt)
    {
        if (auto *listViewport = presetList.getViewport())
        {
            if (adopt)
                listViewport->setViewPosition(0, browser.presetScrollY);
            const int actualY = std::clamp(listViewport->getViewPositionY(), 0, 10000);
            if (actualY != browser.presetScrollY)
            {
                browser.presetScrollY = actualY;
                persistBrowser();
            }
        }
    }
    void knobBounds(ParameterId id, juce::Rectangle<int> area)
    {
        if (auto &knob = knobs[parameterIndex(id)])
            knob->bounds(area);
    }
    void rowOfKnobs(std::initializer_list<ParameterId> ids, juce::Rectangle<int> area)
    {
        const int width = area.getWidth() / std::max(1, int(ids.size()));
        for (auto id : ids)
            knobBounds(id, area.removeFromLeft(width).reduced(2));
    }
    void layout()
    {
        const juce::ScopedValueSetter<bool> noNavigationEcho(synchronizing, true);
        activeEffect = std::clamp(browser.effectPage, 0, 4);
        octave = std::clamp(browser.octave, -1, 4);
        auto bounds = editor.getLocalBounds();
        // Header: logo + name, preset display with navigation, file actions, master + meters.
        headerArea = bounds.removeFromTop(66);
        auto header = headerArea.reduced(14, 9);
        auto brandBox = header.removeFromLeft(std::clamp(header.getWidth() / 6, 150, 210));
        brandBox.removeFromLeft(46); // painted logo mark
        heading.setBounds(brandBox.removeFromTop(28).withTrimmedTop(4));
        brand.setBounds(brandBox.removeFromTop(16));
        auto masterBox = header.removeFromRight(206);
        meterArea = masterBox.removeFromRight(14).reduced(0, 3);
        masterBox.removeFromRight(8);
        auto &master = *knobs[parameterIndex(ParameterId::masterDb)];
        master.slider.setBounds(masterBox.removeFromRight(124).withSizeKeepingCentre(124, 46));
        master.name.setBounds(masterBox.withTrimmedRight(4));
        header.removeFromRight(12);
        auto fileTools = header.removeFromRight(std::min(200, header.getWidth() / 4));
        const int fileWidth = fileTools.getWidth() / 2;
        save.setBounds(fileTools.removeFromLeft(fileWidth).withSizeKeepingCentre(fileWidth - 6, 30));
        load.setBounds(fileTools.withSizeKeepingCentre(fileWidth - 6, 30));
        header.removeFromRight(12);
        auto navigation = header.withSizeKeepingCentre(std::min(header.getWidth(), 560), header.getHeight());
        previous.setBounds(navigation.removeFromLeft(32).withSizeKeepingCentre(32, 32));
        favouriteButton.setBounds(navigation.removeFromRight(34).withSizeKeepingCentre(34, 32));
        navigation.removeFromRight(6);
        next.setBounds(navigation.removeFromRight(32).withSizeKeepingCentre(32, 32));
        displayArea = navigation.reduced(6, 0);
        auto display = displayArea.reduced(12, 3);
        presetHeading.setBounds(display.removeFromTop(display.getHeight() * 3 / 5));
        sourceBadge.setBounds(display);

        // Footer: performance wheels, playable keyboard, octave + voice readout, status.
        footerArea = bounds.removeFromBottom(112);
        auto footer = footerArea.reduced(12, 8);
        status.setBounds(footer.removeFromBottom(16));
        footer.removeFromBottom(4);
        auto wheels = footer.removeFromLeft(72);
        knobBounds(ParameterId::pitchBend, wheels.removeFromLeft(36));
        knobBounds(ParameterId::modWheel, wheels);
        footer.removeFromLeft(8);
        auto output = footer.removeFromRight(116);
        footer.removeFromRight(8);
        octaveLabel.setBounds(output.removeFromTop(20));
        auto octaveButtons = output.removeFromTop(28);
        octaveDown.setBounds(octaveButtons.removeFromLeft(56).reduced(2, 0));
        octaveUp.setBounds(octaveButtons.removeFromRight(56).reduced(2, 0));
        output.removeFromTop(6);
        voiceCount.setBounds(output.removeFromTop(18));
        keyboard.setBounds(footer);
        keyboard.setKeyWidth(std::clamp(float(footer.getWidth()) / 50.0f, 18.0f, 30.0f));

        // Workspace: browser column + a fixed grid of modules that fits without scrolling
        // at ordinary window sizes. Smaller windows scroll instead of crushing controls.
        bounds.reduce(10, 8);
        viewport.setBounds(bounds);
        constexpr int gap = 8, minimumWidth = 1000, minimumHeight = 520;
        const bool scrollX = bounds.getWidth() < minimumWidth, scrollY = bounds.getHeight() < minimumHeight;
        const int bar = viewport.getScrollBarThickness();
        content.setSize(std::max(minimumWidth, bounds.getWidth() - (scrollY ? bar : 0)),
                        std::max(minimumHeight, bounds.getHeight() - (scrollX ? bar : 0)));
        auto work = content.getLocalBounds();
        browserArea = work.removeFromLeft(std::clamp(work.getWidth() * 22 / 100, 236, 300));
        work.removeFromLeft(gap);
        auto browse = browserArea.reduced(10, 8).withTrimmedTop(cardHeader - 2);
        search.setBounds(browse.removeFromTop(28));
        browse.removeFromTop(8);
        // Genres are compact chips (three per row) instead of a full-height column.
        constexpr int columns = 3, chipHeight = 24, chipGap = 4;
        const int chipWidth = (browse.getWidth() + chipGap) / columns;
        for (size_t index = 0; index < genres.size(); ++index)
        {
            const int column = int(index) % columns, row = int(index) / columns;
            genres[index]->setBounds(browse.getX() + column * chipWidth,
                                     browse.getY() + row * (chipHeight + chipGap), chipWidth - chipGap,
                                     chipHeight);
        }
        const int chipRows = (int(genres.size()) + columns - 1) / columns;
        browse.removeFromTop(chipRows * (chipHeight + chipGap) + 4);
        family.setBounds(browse.removeFromTop(28));
        browse.removeFromTop(8);
        presetList.setBounds(browse);
        emptyPresets.setBounds(browse.reduced(8).withHeight(110));

        const int rowHeight = (work.getHeight() - 3 * gap) / 4;
        const auto takeRow = [&]
        {
            auto row = work.removeFromTop(rowHeight);
            work.removeFromTop(gap);
            return row;
        };
        const auto split = [gap](juce::Rectangle<int> &row, int width)
        {
            auto left = row.removeFromLeft(width);
            row.removeFromLeft(gap);
            return left;
        };
        auto row = takeRow();
        macroArea = row;
        row = takeRow();
        oscillatorArea = split(row, (row.getWidth() - gap) / 2);
        unisonArea = row;
        row = takeRow();
        filterArea = split(row, (row.getWidth() - gap) / 2);
        envelopeArea = row;
        row = work;
        modulationArea = split(row, row.getWidth() * 26 / 100);
        voiceArea = split(row, (row.getWidth() - gap) * 55 / 100);
        effectsArea = row;
        const auto body = [](juce::Rectangle<int> area)
        { return area.withTrimmedTop(cardHeader).reduced(8, 5); };
        // Choices sit in the module header, right of its title.
        const auto headerSlot = [](juce::Rectangle<int> area, int title)
        { return area.withHeight(cardHeader).withTrimmedLeft(title).reduced(10, 4); };

        auto macros = body(macroArea);
        presetDescription.setBounds(macros.removeFromLeft(macros.getWidth() * 34 / 100).reduced(6, 4));
        rowOfKnobs({ParameterId::tone, ParameterId::warmth, ParameterId::space, ParameterId::motion}, macros);

        auto oscillatorChoices = headerSlot(oscillatorArea, 120);
        const int choiceWidth = std::min(126, oscillatorChoices.getWidth() / 3);
        oscillator2.setBounds(oscillatorChoices.removeFromRight(choiceWidth).reduced(2, 0));
        oscillator1.setBounds(oscillatorChoices.removeFromRight(choiceWidth).reduced(2, 0));
        sourceType.setBounds(oscillatorChoices.removeFromRight(choiceWidth).reduced(2, 0));
        rowOfKnobs({ParameterId::oscillatorBlend, ParameterId::detuneCents, ParameterId::oscillator2Semitones,
                    ParameterId::pulseWidth, ParameterId::fmAmount, ParameterId::sampleLevel},
                   body(oscillatorArea));
        rowOfKnobs({ParameterId::unisonVoices, ParameterId::unisonDetuneCents, ParameterId::unisonSpread,
                    ParameterId::subLevel, ParameterId::noiseLevel, ParameterId::analogDrift},
                   body(unisonArea));

        auto filterChoices = headerSlot(filterArea, 90);
        const int filterChoiceWidth = std::min(130, filterChoices.getWidth() / 2);
        filterModel.setBounds(filterChoices.removeFromRight(filterChoiceWidth).reduced(2, 0));
        filterType.setBounds(filterChoices.removeFromRight(filterChoiceWidth).reduced(2, 0));
        rowOfKnobs({ParameterId::cutoffHz, ParameterId::resonance, ParameterId::filterDrive,
                    ParameterId::filterEnvelopeOctaves, ParameterId::filterKeyTrack,
                    ParameterId::filterVelocityOctaves},
                   body(filterArea));
        rowOfKnobs({ParameterId::attackMs, ParameterId::decayMs, ParameterId::sustain, ParameterId::releaseMs,
                    ParameterId::envelopeCurve, ParameterId::velocityToAmp},
                   body(envelopeArea));
        rowOfKnobs({ParameterId::modAttackMs, ParameterId::modDecayMs, ParameterId::modSustain,
                    ParameterId::wavetableBank, ParameterId::wavetablePosition, ParameterId::wavetableEnvelope},
                   body(modulationArea));

        auto modeSlot = headerSlot(voiceArea, 70);
        voiceMode.setBounds(modeSlot.removeFromRight(std::min(110, modeSlot.getWidth())));
        rowOfKnobs({ParameterId::glideMs, ParameterId::polyphony, ParameterId::bendRangeSemitones,
                    ParameterId::vibratoRateHz, ParameterId::vibratoDepthCents, ParameterId::pan},
                   body(voiceArea));

        // Effects: the header is the page selector; each tab carries its power LED.
        auto fxTabs = effectsArea.withHeight(cardHeader).reduced(8, 4);
        const int tabWidth = fxTabs.getWidth() / 5;
        for (size_t index = 0; index < effectTabs.size(); ++index)
        {
            auto tab = fxTabs.removeFromLeft(tabWidth).reduced(1, 0);
            effectEnabled[index].setBounds(tab.removeFromLeft(18));
            effectTabs[index].setBounds(tab);
            effectTabs[index].setToggleState(activeEffect == int(index), juce::dontSendNotification);
        }
        const auto fxControls = body(effectsArea);
        const std::array<std::initializer_list<ParameterId>, 5> groups{
            {{ParameterId::eqLowDb, ParameterId::eqMidDb, ParameterId::eqHighDb},
             {ParameterId::compressorThresholdDb, ParameterId::compressorRatio},
             {ParameterId::chorusMix},
             {ParameterId::delayTimeMs, ParameterId::delayFeedback, ParameterId::delayMix},
             {ParameterId::reverbSize, ParameterId::reverbDecaySeconds, ParameterId::reverbMix}}};
        for (size_t group = 0; group < groups.size(); ++group)
            for (auto id : groups[group])
            {
                knobs[parameterIndex(id)]->slider.setVisible(int(group) == activeEffect);
                knobs[parameterIndex(id)]->name.setVisible(int(group) == activeEffect);
            }
        // Effect pages keep a fixed knob width so one-knob pages do not stretch.
        const auto &page = groups[size_t(activeEffect)];
        rowOfKnobs(page, fxControls.withWidth(int(page.size()) * fxControls.getWidth() / 3));
        octaveLabel.setText("Octave " + juce::String(octave), juce::dontSendNotification);
        const int firstNote = 36 + octave * 12;
        // JUCE resets firstKey to rangeStart when its stock scroll buttons are
        // disabled. Set the displayed range itself so our octave controls move
        // real keys at every window width, including a wide viewport.
        if (keyboard.getRangeStart() != firstNote || keyboard.getRangeEnd() != 127)
        {
            keyboard.releaseHeldInput();
            keyboard.setAvailableRange(firstNote, 127);
        }
        keyboard.setLowestVisibleKey(firstNote);
        octaveDown.setEnabled(octave > -1);
        octaveUp.setEnabled(octave < 4);
        viewport.setViewPosition(browser.scrollX, browser.scrollY);
        const int actualX = viewport.getViewPositionX(), actualY = viewport.getViewPositionY();
        if (browser.scrollX != actualX || browser.scrollY != actualY)
        {
            browser.scrollX = actualX;
            browser.scrollY = actualY;
            persistBrowser();
        }
        synchronizePresetScroll(true);
        content.repaint();
        editor.repaint();
    }
    // Compact envelope shape drawn in a module header.
    static void drawEnvelope(juce::Graphics &g, juce::Rectangle<float> plot, float a, float d, float s,
                             float r, float curve)
    {
        g.setColour(inset);
        g.fillRoundedRectangle(plot.expanded(4, 2), 4);
        const float total = std::max(1.0f, a + d + r + 600);
        const float x0 = plot.getX(), x1 = x0 + plot.getWidth() * a / total,
                    x2 = x1 + plot.getWidth() * d / total, x3 = x2 + plot.getWidth() * 600 / total,
                    x4 = plot.getRight();
        const float top = plot.getY(), bottom = plot.getBottom(), level = bottom - plot.getHeight() * s;
        // Curve zero draws straight segments; one bends them like the analogue segments.
        const auto bend = [curve](juce::Point<float> from, juce::Point<float> to, juce::Point<float> corner)
        { return (from + to) * .5f + (corner - (from + to) * .5f) * curve; };
        juce::Path envelope;
        envelope.startNewSubPath(x0, bottom);
        envelope.quadraticTo(bend({x0, bottom}, {x1, top}, {x0 + (x1 - x0) * .35f, top}), {x1, top});
        envelope.quadraticTo(bend({x1, top}, {x2, level}, {x1 + (x2 - x1) * .15f, level}), {x2, level});
        envelope.lineTo(x3, level);
        envelope.quadraticTo(bend({x3, level}, {x4, bottom}, {x3 + (x4 - x3) * .15f, bottom}), {x4, bottom});
        g.setColour(envColour);
        g.strokePath(envelope, juce::PathStrokeType(1.5f));
        envelope.lineTo(x0, bottom);
        envelope.closeSubPath();
        g.setColour(envColour.withAlpha(.16f));
        g.fillPath(envelope);
    }
    void paintContent(juce::Graphics &g)
    {
        g.fillAll(background);
        card(g, browserArea, "BROWSER", accent);
        card(g, macroArea, "MACROS", macroColour);
        card(g, oscillatorArea, "OSCILLATORS", oscColour);
        card(g, unisonArea, "UNISON  /  SUB  /  NOISE", unisonColour);
        card(g, filterArea, "FILTER", filterColour);
        card(g, envelopeArea, "AMP ENVELOPE", envColour);
        card(g, modulationArea, "MOD ENV  /  WAVETABLE", envColour);
        card(g, voiceArea, "VOICE", voiceColour);
        card(g, effectsArea);
        g.setColour(border.withAlpha(.7f));
        g.fillRect(effectsArea.getX() + 10, effectsArea.getY() + cardHeader, effectsArea.getWidth() - 20, 1);
        const float curve = nativeValue(ParameterId::envelopeCurve);
        const auto plot = [](juce::Rectangle<int> area, int title)
        {
            auto slot = area.withHeight(cardHeader).withTrimmedLeft(title).reduced(16, 8);
            return slot.removeFromRight(std::min(170, slot.getWidth())).toFloat();
        };
        drawEnvelope(g, plot(envelopeArea, 140), nativeValue(ParameterId::attackMs),
                     nativeValue(ParameterId::decayMs), nativeValue(ParameterId::sustain),
                     nativeValue(ParameterId::releaseMs), curve);
        if (nativeValue(ParameterId::modDecayMs) > 0)
            drawEnvelope(g, plot(modulationArea, 90), nativeValue(ParameterId::modAttackMs),
                         nativeValue(ParameterId::modDecayMs), nativeValue(ParameterId::modSustain),
                         nativeValue(ParameterId::releaseMs), curve);
        else
        {
            g.setColour(muted);
            g.setFont(uiFont(11));
            g.drawText("follows amp", plot(modulationArea, 90).toNearestInt(),
                       juce::Justification::centredRight);
        }
    }
};

SynthEditor::SynthEditor(juce::AudioProcessor &processor, juce::MidiKeyboardState &keyboard,
                         Callbacks callbacks)
    : AudioProcessorEditor(processor),
      impl_(std::make_unique<Impl>(*this, processor, keyboard, std::move(callbacks)))
{
    // paint() fills the complete editor with an opaque background. Declare this
    // for native peers too, so a directly hosted editor needs no alpha bitmap
    // before Windows can hit-test its controls.
    setOpaque(true);
    setResizable(true, false);
    setResizeLimits(760, 540, 2400, 1600);
    setSize(1360, 860);
    startTimerHz(30);
}
SynthEditor::~SynthEditor()
{
    stopTimer();
    impl_.reset();
}
void SynthEditor::paint(juce::Graphics &g)
{
    g.fillAll(background);
    const auto &self = *impl_;
    // Header bar with an amber rule: the instrument's own face, not the host chrome.
    const auto header = self.headerArea.toFloat();
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff1d1e27), 0, header.getY(),
                                           juce::Colour(0xff121319), 0, header.getBottom(), false));
    g.fillRect(header);
    g.setGradientFill(juce::ColourGradient(accent.withAlpha(0.0f), header.getX(), 0, accent.withAlpha(.7f),
                                           header.getCentreX(), 0, false));
    g.fillRect(header.withTop(header.getBottom() - 1).withWidth(header.getWidth() * .5f));
    g.setGradientFill(juce::ColourGradient(accent.withAlpha(.7f), header.getCentreX(), 0,
                                           accent.withAlpha(0.0f), header.getRight(), 0, false));
    g.fillRect(header.withTop(header.getBottom() - 1).withLeft(header.getCentreX()));
    // Logo mark: an amber tile carrying a saw-to-sine stroke.
    const auto logo = juce::Rectangle<float>(header.getX() + 14, header.getCentreY() - 17, 34, 34);
    g.setGradientFill(juce::ColourGradient(accent.brighter(.2f), logo.getX(), logo.getY(),
                                           accent.darker(.45f), logo.getRight(), logo.getBottom(), false));
    g.fillRoundedRectangle(logo, 8);
    juce::Path wave;
    const auto inner = logo.reduced(7, 9);
    wave.startNewSubPath(inner.getX(), inner.getBottom());
    wave.lineTo(inner.getX() + inner.getWidth() * .3f, inner.getY());
    wave.lineTo(inner.getX() + inner.getWidth() * .3f, inner.getBottom());
    wave.quadraticTo(inner.getX() + inner.getWidth() * .5f, inner.getY() - 4,
                     inner.getX() + inner.getWidth() * .65f, inner.getCentreY());
    wave.quadraticTo(inner.getX() + inner.getWidth() * .82f, inner.getBottom() + 4, inner.getRight(),
                     inner.getY() + 2);
    g.setColour(background);
    g.strokePath(wave,
                 juce::PathStrokeType(2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    // Preset display.
    const auto display = self.displayArea.toFloat();
    if (!display.isEmpty())
    {
        g.setColour(juce::Colour(0xff07080a));
        g.fillRoundedRectangle(display, 6);
        g.setGradientFill(juce::ColourGradient(accent.withAlpha(.07f), 0, display.getY(),
                                               accent.withAlpha(0.0f), 0, display.getCentreY(), false));
        g.fillRoundedRectangle(display, 6);
        g.setColour(border.brighter(.1f));
        g.drawRoundedRectangle(display.reduced(.5f), 6, 1);
    }
    // Stereo output meters beside Master.
    auto meter = self.meterArea.toFloat();
    for (const float peak : {self.peakLeft, self.peakRight})
    {
        const auto bar = meter.removeFromLeft(5);
        meter.removeFromLeft(3);
        g.setColour(inset);
        g.fillRoundedRectangle(bar, 2);
        const float decibels =
            peak > 0 && std::isfinite(peak) ? juce::Decibels::gainToDecibels(peak, -60.0f) : -60.0f;
        const float proportion = std::clamp((decibels + 60.0f) / 60.0f, 0.0f, 1.0f);
        const auto lit = bar.withTop(bar.getBottom() - bar.getHeight() * proportion);
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xffff5a4a), 0, bar.getY(), voiceColour, 0,
                                               bar.getBottom(), false));
        g.fillRoundedRectangle(lit, 2);
    }
    // Footer deck behind the keyboard.
    const auto footer = self.footerArea.toFloat();
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff15161c), 0, footer.getY(),
                                           juce::Colour(0xff0b0c0f), 0, footer.getBottom(), false));
    g.fillRect(footer);
    g.setColour(border);
    g.fillRect(footer.withHeight(1));
}
void SynthEditor::resized()
{
    if (impl_)
        impl_->layout();
}
void SynthEditor::timerCallback()
{
    impl_->tick();
}
void SynthEditor::focusLost(FocusChangeType)
{
    if (impl_ && !hasKeyboardFocus(true))
        impl_->releaseInput();
}
void SynthEditor::visibilityChanged()
{
    if (impl_ && !isShowing())
        impl_->releaseInput();
}
} // namespace vibera::synth
