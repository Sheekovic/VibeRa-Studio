// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "SamplerSettings.h"
#include "Sampler.h"
#include "SamplerWaveform.h"
#include "Theme.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include <functional>

namespace vibera
{
// UI-only. Its state/listeners never run in the audio callback.
class SamplerKeyboard final : public juce::Component, private juce::MidiKeyboardState::Listener
{
    juce::MidiKeyboardState state;
    class Keys final : public juce::MidiKeyboardComponent
    {
        SamplerKeyboard &owner;
        bool mouseDownOnKey(int pitch, const juce::MouseEvent &event) override
        {
            if (!owner.isEnabled())
                return false;
            if (event.mods.isRightButtonDown())
            {
                if (owner.onRoot)
                    owner.onRoot(pitch);
                return false;
            }
            return event.mods.isLeftButtonDown();
        }
        bool mouseDraggedToKey(int, const juce::MouseEvent &event) override
        {
            return owner.isEnabled() && event.mods.isLeftButtonDown() && !event.mods.isRightButtonDown();
        }
        void drawWhiteNote(int pitch, juce::Graphics &g, juce::Rectangle<float> area, bool down, bool over,
                           juce::Colour line, juce::Colour text) override
        {
            MidiKeyboardComponent::drawWhiteNote(pitch, g, area, down, over, line, text);
            if (pitch == owner.root)
            {
                g.setColour(accent);
                g.fillRect(area.withTop(area.getBottom() - 5));
            }
        }
        void drawBlackNote(int pitch, juce::Graphics &g, juce::Rectangle<float> area, bool down, bool over,
                           juce::Colour colour) override
        {
            MidiKeyboardComponent::drawBlackNote(pitch, g, area, down, over, colour);
            if (pitch == owner.root)
            {
                g.setColour(accent);
                g.drawRect(area.reduced(1), 2);
            }
        }
        void focusLost(FocusChangeType cause) override
        {
            juce::ignoreUnused(cause);
            owner.release();
        }

      public:
        void clearHeld()
        {
            MidiKeyboardComponent::focusLost(juce::Component::focusChangedDirectly);
        }
        explicit Keys(SamplerKeyboard &o) : MidiKeyboardComponent(o.state, horizontalKeyboard), owner(o)
        {
            clearKeyMappings(); // Preserve contextual DAW bindings and the typing-keyboard feature.
            setAvailableRange(0, 127);
            setLowestVisibleKey(48);
            setKeyWidth(22);
            setOctaveForMiddleC(4); // Same MIDI/octave convention as the existing Piano Roll.
            setVelocity(1.f, true);
            setWantsKeyboardFocus(true);
        }
    } keys{*this};
    int root = 60;
    void handleNoteOn(juce::MidiKeyboardState *, int, int pitch, float velocity) override
    {
        if (onNote)
            onNote(pitch, true, juce::jlimit(1, 127, juce::roundToInt(velocity * 127)));
    }
    void handleNoteOff(juce::MidiKeyboardState *, int, int pitch, float) override
    {
        if (onNote)
            onNote(pitch, false, 0);
    }
    void visibilityChanged() override
    {
        if (!isShowing())
            release();
    }
    void enablementChanged() override
    {
        if (!isEnabled())
            release();
    }

  public:
    std::function<void(int, bool, int)> onNote;
    std::function<void(int)> onRoot;
    std::function<void()> onRelease;
    SamplerKeyboard()
    {
        state.addListener(this);
        addAndMakeVisible(keys);
        setComponentID("instrument.sampler.keyboard");
        applyTheme();
    }
    ~SamplerKeyboard() override
    {
        release();
        state.removeListener(this);
    }
    void applyTheme()
    {
        keys.setColour(juce::MidiKeyboardComponent::whiteNoteColourId, ink.darker(.12f));
        keys.setColour(juce::MidiKeyboardComponent::blackNoteColourId, bg);
        keys.setColour(juce::MidiKeyboardComponent::keySeparatorLineColourId, grid);
        keys.setColour(juce::MidiKeyboardComponent::textLabelColourId, bg);
        keys.setColour(juce::MidiKeyboardComponent::keyDownOverlayColourId, accent.withAlpha(.75f));
        keys.setColour(juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, accent.withAlpha(.25f));
    }
    void setRoot(int pitch)
    {
        if (root != pitch)
        {
            root = pitch;
            keys.repaint();
        }
    }
    void release()
    {
        keys.clearHeld();
        state.allNotesOff(0);
        if (onRelease)
            onRelease();
    }
    void resized() override
    {
        keys.setBounds(getLocalBounds());
    }
    juce::MidiKeyboardComponent &keyComponent()
    {
        return keys;
    }
};

class SamplerRangeView final : public juce::Component,
                               public juce::SettableTooltipClient,
                               private juce::ScrollBar::Listener
{
    std::shared_ptr<const Sample> source;
    SamplerWaveform waveform;
    juce::ScrollBar scroll{false};
    double viewBegin = 0, viewSpan = 1;
    bool dragging = false, startHandle = true, loopHandle = false;
    double initialStart = 0, initialEnd = 1;
    float initialX = 0;
    float xFor(double position) const
    {
        return 8.f + float((position - viewBegin) / viewSpan) * float(std::max(1, getWidth() - 16));
    }
    void update(float x, bool together)
    {
        if (!dragging || !settings || !change)
            return;
        const auto current = settings();
        const double lower = loopHandle && current.loop ? current.start : 0.;
        const double upper = loopHandle && current.loop ? current.end : 1.;
        auto &rangeCommand = loopHandle ? changeLoopRange : changeRange;
        if (together && rangeCommand)
        {
            const auto length = initialEnd - initialStart;
            const auto start = juce::jlimit(
                0., 1. - length, initialStart + viewSpan * (x - initialX) / std::max(1, getWidth() - 16));
            rangeCommand(start, start + length);
            repaint();
            return;
        }
        const auto s = settings();
        const auto minimum = 1. / std::max(1, frames ? frames() - 1 : 1);
        const double position = viewBegin + viewSpan * (x - 8.) / std::max(1, getWidth() - 16);
        auto &command = loopHandle ? changeLoop : change;
        if (command)
            command(startHandle,
                    startHandle
                        ? juce::jlimit(lower, std::max(lower, (loopHandle ? s.loopEnd : s.end) - minimum),
                                       position)
                        : juce::jlimit(std::min(upper, (loopHandle ? s.loopStart : s.start) + minimum), upper,
                                       position));
        repaint();
    }
    void visibilityChanged() override
    {
        if (!isShowing())
            finish(true);
    }
    void enablementChanged() override
    {
        if (!isEnabled())
            finish(true);
    }

  public:
    std::function<juce::String(const juce::KeyPress &)> resolveKey;
    std::function<SamplerSettings()> settings;
    std::function<int()> frames;
    std::function<void(bool, double)> change;
    std::function<void(double, double)> changeRange;
    std::function<void(bool, double)> changeLoop;
    std::function<void(double, double)> changeLoopRange;
    std::function<void(int)> tempoAction;
    std::function<bool()> begin;
    std::function<void(bool)> end; // true cancels to the original document snapshot.
    SamplerRangeView()
    {
        setComponentID("instrument.sampler.waveform");
        setWantsKeyboardFocus(true);
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
        addAndMakeVisible(scroll);
        scroll.setComponentID("instrument.sampler.waveform.scroll");
        scroll.addListener(this);
        scroll.setRangeLimits(0, 1);
        scroll.setCurrentRange(0, 1);
        scroll.setAutoHide(false);
        setTooltip("Wheel: zoom at pointer. Shift-wheel: pan. Right-click: view and tempo tools.");
    }
    ~SamplerRangeView() override
    {
        finish(true);
    }
    void finish(bool cancel)
    {
        if (dragging)
        {
            dragging = false;
            if (end)
                end(cancel);
        }
    }
    void setSample(std::shared_ptr<const Sample> next)
    {
        if (next == source)
            return;
        finish(false);
        source = std::move(next);
        if (source)
            waveform.prepare(source->data);
        else
            waveform.prepare(juce::AudioBuffer<float>{});
        setView(0, 1);
    }
    double viewStart() const noexcept
    {
        return viewBegin;
    }
    double viewLength() const noexcept
    {
        return viewSpan;
    }
    float sourceX(double position) const noexcept
    {
        return xFor(position);
    }
    void setView(double begin, double span)
    {
        if (dragging || !std::isfinite(begin) || !std::isfinite(span))
            return;
        const auto minimum = 1. / std::max(2, frames ? frames() : 2);
        viewSpan = juce::jlimit(minimum, 1., span);
        viewBegin = juce::jlimit(0., 1. - viewSpan, begin);
        scroll.setCurrentRange(viewBegin, viewSpan, juce::dontSendNotification);
        repaint();
    }
    void navigation(const juce::String &action)
    {
        if (action == "sampler.view.all")
            setView(0, 1);
        else if (action == "sampler.view.range" && settings)
        {
            const auto value = settings();
            setView(value.start, value.end - value.start);
        }
        else if (action == "sampler.view.zoomIn" || action == "sampler.view.zoomOut")
        {
            const auto span = std::min(1., viewSpan * (action == "sampler.view.zoomIn" ? .5 : 2.));
            setView(viewBegin + (viewSpan - span) * .5, span);
        }
    }
    void mouseWheelMove(const juce::MouseEvent &event, const juce::MouseWheelDetails &wheel) override
    {
        if (!isEnabled() || dragging)
            return;
        if (event.mods.isShiftDown() || std::abs(wheel.deltaX) > std::abs(wheel.deltaY))
        {
            const auto delta = std::abs(wheel.deltaX) > std::abs(wheel.deltaY) ? wheel.deltaX : wheel.deltaY;
            setView(viewBegin - delta * viewSpan, viewSpan);
        }
        else
        {
            const double fraction =
                juce::jlimit(0., 1., (event.position.x - 8.) / std::max(1, getWidth() - 16));
            const double span = juce::jlimit(1. / std::max(2, frames ? frames() : 2), 1.,
                                             viewSpan / std::pow(2., wheel.deltaY * 3.));
            setView(viewBegin + fraction * (viewSpan - span), span);
        }
    }
    void scrollBarMoved(juce::ScrollBar *, double begin) override
    {
        setView(begin, viewSpan);
    }
    void resized() override
    {
        scroll.setBounds(8, getHeight() - scrollBarThickness, std::max(1, getWidth() - 16),
                         scrollBarThickness);
    }
    void paint(juce::Graphics &g) override
    {
        g.setColour(bg);
        g.fillRoundedRectangle(getLocalBounds().toFloat(), 4);
        const auto body = juce::Rectangle<float>(8, 16, float(std::max(1, getWidth() - 16)),
                                                 float(std::max(1, getHeight() - scrollBarThickness - 18)));
        if (source && source->data.getNumSamples() > 0)
        {
            const int n = source->data.getNumSamples();
            g.setColour(muted);
            g.setFont(11.f);
            for (int tick = 0; tick <= 4; ++tick)
            {
                const double position = viewBegin + viewSpan * tick / 4.;
                g.drawText(viewSpan * n < 1024 ? juce::String(int(std::round(position * n))) + "f"
                                               : juce::String(position * n / source->sampleRate, 3) + "s",
                           int(body.getX() + tick * body.getWidth() / 4) - (tick == 4 ? 64 : 0), 0, 64, 14,
                           juce::Justification::centredLeft);
            }
            juce::Graphics::ScopedSaveState clip(g);
            g.reduceClipRegion(body.toNearestInt());
            g.setColour(accent);
            const double normalization = settings && settings().normalize ? source->normalizationGain : 1.;
            for (int x = 0; x < int(body.getWidth()); ++x)
            {
                int begin = int((viewBegin + viewSpan * x / body.getWidth()) * n);
                int end = std::max(begin + 1, int((viewBegin + viewSpan * (x + 1) / body.getWidth()) * n));
                const auto extent = waveform.query(source->data, begin, end);
                const auto top =
                    body.getCentreY() - float(double(extent.high) * normalization) * body.getHeight() * .45f;
                const auto bottom =
                    body.getCentreY() - float(double(extent.low) * normalization) * body.getHeight() * .45f;
                g.drawVerticalLine(int(body.getX()) + x, top, std::max(top + 1, bottom));
            }
        }
        if (settings)
        {
            const auto s = settings();
            const auto left = xFor(s.start), right = xFor(s.end);
            g.setColour(bg.withAlpha(.75f));
            const auto clippedLeft = juce::jlimit(body.getX(), body.getRight(), left);
            const auto clippedRight = juce::jlimit(body.getX(), body.getRight(), right);
            g.fillRect(body.withRight(clippedLeft));
            g.fillRect(juce::Rectangle<float>(clippedRight, body.getY(), body.getRight() - clippedRight,
                                              body.getHeight()));
            g.setColour(accent);
            for (bool start : {true, false})
            {
                const auto position = start ? s.start : s.end;
                if (position < viewBegin || position > viewBegin + viewSpan)
                    continue;
                const auto x = xFor(position);
                g.drawVerticalLine(int(x), 0, body.getBottom());
                juce::Path handle;
                if (start)
                    handle.addTriangle(x - 7, 0, x + 7, 0, x, 12);
                else
                    handle.addTriangle(x - 7, body.getBottom(), x + 7, body.getBottom(), x,
                                       body.getBottom() - 12);
                g.fillPath(handle);
            }
            if (s.loop)
            {
                g.setColour(ink);
                const float centre = body.getCentreY();
                for (double position : {s.loopStart, s.loopEnd})
                {
                    if (position < viewBegin || position > viewBegin + viewSpan)
                        continue;
                    const float x = xFor(position);
                    g.drawVerticalLine(int(x), body.getY(), body.getBottom());
                    g.fillRoundedRectangle(x - 6, centre - 7, 12, 14, 2);
                }
            }
        }
    }
    void mouseDown(const juce::MouseEvent &event) override
    {
        if (!isEnabled() || !settings || !frames || frames() < 2)
            return;
        if (event.mods.isRightButtonDown())
        {
            juce::PopupMenu menu;
            menu.addItem(1, "Reset source range");
            menu.addSeparator();
            menu.addItem(2, "Set tempo...");
            menu.addItem(3, "Detect tempo");
            menu.addItem(4, "Fit to tempo / Unfit");
            menu.addSeparator();
            menu.addItem(5, "Show full sample");
            menu.addItem(6, "Zoom to source range");
            menu.addItem(7, "Zoom in");
            menu.addItem(8, "Zoom out");
            juce::Component::SafePointer<SamplerRangeView> safe(this);
            menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),
                               [safe](int result)
                               {
                                   if (safe && result >= 5)
                                       safe->navigation(result == 5   ? "sampler.view.all"
                                                        : result == 6 ? "sampler.view.range"
                                                        : result == 7 ? "sampler.view.zoomIn"
                                                                      : "sampler.view.zoomOut");
                                   if (safe && result >= 2 && result <= 4 && safe->tempoAction)
                                       safe->tempoAction(result);
                                   if (safe && result == 1 && safe->changeRange &&
                                       (!safe->begin || safe->begin()))
                                   {
                                       safe->changeRange(0, 1);
                                       if (safe->end)
                                           safe->end(false);
                                       safe->repaint();
                                   }
                               });
            return;
        }
        if (!event.mods.isLeftButtonDown())
            return;
        const auto s = settings();
        loopHandle = s.loop && changeLoop && std::abs(event.position.y - (getHeight() - 4) * .5f) <= 14 &&
                     std::min(std::abs(event.position.x - xFor(s.loopStart)),
                              std::abs(event.position.x - xFor(s.loopEnd))) <= 12;
        const double beginPosition = loopHandle ? s.loopStart : s.start;
        const double endPosition = loopHandle ? s.loopEnd : s.end;
        const float left = beginPosition < viewBegin || beginPosition > viewBegin + viewSpan
                               ? 1e9f
                               : std::abs(event.position.x - xFor(beginPosition));
        const float right = endPosition < viewBegin || endPosition > viewBegin + viewSpan
                                ? 1e9f
                                : std::abs(event.position.x - xFor(endPosition));
        if (std::min(left, right) > 12)
            return;
        startHandle = left == right ? event.position.y < getHeight() / 2 : left < right;
        if (begin && !begin())
            return;
        initialStart = beginPosition;
        initialEnd = endPosition;
        initialX = event.position.x;
        dragging = true;
        grabKeyboardFocus();
    }
    void mouseDrag(const juce::MouseEvent &event) override
    {
        update(event.position.x, event.mods.isShiftDown());
    }
    void mouseUp(const juce::MouseEvent &) override
    {
        finish(false);
    }
    bool keyPressed(const juce::KeyPress &key) override
    {
        if (dragging && key.getKeyCode() == juce::KeyPress::escapeKey)
        {
            finish(true);
            repaint();
            return true;
        }
        if (resolveKey)
        {
            const auto action = resolveKey(key);
            if (action.startsWith("sampler.view."))
            {
                navigation(action);
                return true;
            }
        }
        return false;
    }
    void focusLost(FocusChangeType) override
    {
        finish(false);
    }
};
} // namespace vibera
