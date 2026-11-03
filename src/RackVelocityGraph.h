// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Studio.h"
#include "StepVelocity.h"
#include "StepExpression.h"
#include "StepShift.h"
#include "StepRepeat.h"
#include "StepRelease.h"

namespace vibera
{
class RackVelocityGraph final : public juce::Component, public juce::SettableTooltipClient
{
    Studio &studio;
    bool active = false;
    std::uint64_t serial = 0;
    juce::String session, mode;
    juce::String gestureProperty = "velocity";
    juce::ComboBox propertyPicker;
    Id patternId = 0, channelId = 0, anchor = 0;
    int first = 0, last = 0;
    float firstValue = 0, lastValue = 0;
    bool fine = false;
    float fineStartY = 0, fineStartValue = 0;
    std::vector<Id> targets;
    static const StepExpressionInfo *expression(const juce::String &property)
    {
        return property == "pan"    ? stepExpressionInfo(StepExpression::pan)
               : property == "fine" ? stepExpressionInfo(StepExpression::fine)
               : property == "modX" ? stepExpressionInfo(StepExpression::modX)
               : property == "modY" ? stepExpressionInfo(StepExpression::modY)
                                    : nullptr;
    }
    static int propertyId(const juce::String &property)
    {
        return property == "pitch"     ? 2
               : property == "pan"     ? 3
               : property == "fine"    ? 4
               : property == "shift"   ? 5
               : property == "repeat"  ? 6
               : property == "release" ? 7
               : property == "modX"    ? 8
               : property == "modY"    ? 9
                                       : 1;
    }
    static juce::String propertyFor(int id)
    {
        return id == 2   ? "pitch"
               : id == 3 ? "pan"
               : id == 4 ? "fine"
               : id == 5 ? "shift"
               : id == 6 ? "repeat"
               : id == 7 ? "release"
               : id == 8 ? "modX"
               : id == 9 ? "modY"
                         : "velocity";
    }
    static float normalized(const Note &note, const juce::String &property)
    {
        if (property == "release")
        {
            const auto *info = stepReleaseInfo();
            return float((note.releaseVelocity - info->minimum) / (info->maximum - info->minimum));
        }
        if (property == "repeat")
        {
            const auto *info = stepRepeatInfo();
            return float((note.repeatMode - info->minimum) / (info->maximum - info->minimum));
        }
        if (property == "shift")
        {
            const auto *info = stepShiftInfo();
            return float((note.shiftPercent - info->minimum) / (info->maximum - info->minimum));
        }
        if (const auto *info = expression(property))
        {
            const double stored = stepExpressionValue(note, info->property);
            return float((stored - info->minimum) / (info->maximum - info->minimum));
        }
        return property == "pitch" ? float(note.pitch) / 127.f : note.velocity;
    }
    static bool supported(const Channel &channel, const juce::String &property)
    {
        return !channel.audioClip && (!expression(property) || !channel.instrument);
    }
    int column(juce::Point<float> point) const
    {
        const auto *pattern = studio.controller.project.pattern(studio.selectedPattern);
        const int count = pattern ? std::max(1, int(std::ceil(pattern->length * 4))) : 1;
        const int page = std::max(1, int((getWidth() - left - 8) / width));
        return juce::jlimit(std::min(offset, count - 1), std::min(count - 1, offset + page - 1),
                            offset + int(std::floor((point.x - left) / width)));
    }
    float value(juce::Point<float> point) const
    {
        const auto area = chart();
        return juce::jlimit(0.f, 1.f,
                            fine ? fineStartValue +
                                       (fineStartY - point.y) / (std::max(1.f, area.getHeight()) * 10.f)
                                 : (area.getBottom() - point.y) / std::max(1.f, area.getHeight()));
    }
    void apply(juce::Point<float> point)
    {
        if (!ownsGesture())
        {
            active = false;
            return;
        }
        const auto *pattern = studio.controller.project.pattern(patternId);
        if (!pattern || studio.selectedPattern != patternId || studio.selectedChannel != channelId ||
            studio.rackGraphProperty != gestureProperty || !studio.controller.project.channel(channelId) ||
            !supported(*studio.controller.project.channel(channelId), gestureProperty))
        {
            finish(true);
            return;
        }
        const int step = column(point);
        const auto nextValue = value(point);
        CommandArgs args;
        args.pattern = patternId;
        args.channel = channelId;
        args.text = mode;
        args.target = anchor;
        args.value = nextValue;
        if (mode == "scale")
            args.ids = targets;
        else if (mode == "ramp")
        {
            args.ids = targets;
            args.beat = first * .25;
            args.length = (step - first) * .25;
            args.value = firstValue;
            args.offset = nextValue;
        }
        else
        {
            // Paint continuous segments, including columns skipped by a fast drag.
            for (const auto &note : pattern->notes)
                if (graphStep(note, channelId, pattern->length) &&
                    note.beat >= std::min(step, last) * .25 - 1.e-8 &&
                    note.beat <= std::max(step, last) * .25 + 1.e-8)
                    args.ids.push_back(note.id);
            if (args.ids.empty())
            {
                last = step;
                lastValue = nextValue;
                return;
            }
            if (mode == "set")
            {
                args.text = "ramp";
                args.beat = last * .25;
                args.length = (step - last) * .25;
                args.value = lastValue;
                args.offset = nextValue;
            }
        }
        if (gestureProperty == "pitch")
        {
            args.pitch = juce::roundToInt(args.value * 127);
            args.offset = juce::roundToInt(args.offset * 127);
        }
        const auto *info = expression(gestureProperty);
        if (info)
        {
            args.value = info->minimum + args.value * (info->maximum - info->minimum);
            args.offset = info->minimum + args.offset * (info->maximum - info->minimum);
        }
        if (gestureProperty == "shift")
        {
            const auto *shift = stepShiftInfo();
            args.value = shift->minimum + args.value * (shift->maximum - shift->minimum);
            args.offset = shift->minimum + args.offset * (shift->maximum - shift->minimum);
        }
        if (gestureProperty == "repeat")
        {
            const auto *repeat = stepRepeatInfo();
            args.value = juce::roundToInt(repeat->minimum + args.value * (repeat->maximum - repeat->minimum));
            args.offset =
                juce::roundToInt(repeat->minimum + args.offset * (repeat->maximum - repeat->minimum));
        }
        if (gestureProperty == "release")
        {
            const auto *release = stepReleaseInfo();
            args.value = release->minimum + args.value * (release->maximum - release->minimum);
            args.offset = release->minimum + args.offset * (release->maximum - release->minimum);
        }
        studio.command(gestureProperty == "release"  ? stepReleaseInfo()->command
                       : gestureProperty == "repeat" ? stepRepeatInfo()->command
                       : gestureProperty == "shift"  ? stepShiftInfo()->command
                       : info                        ? info->command
                       : gestureProperty == "pitch"  ? "steps.pitch"
                                                     : "steps.velocity",
                       args);
        if (studio.status != "Ready")
        {
            finish(false);
            return;
        }
        last = step;
        lastValue = nextValue;
        repaint();
    }

  public:
    int left = 210, offset = 0;
    double width = 16;
    std::function<void()> focusRack;
    std::function<void(const juce::MouseEvent &, const juce::MouseWheelDetails &)> wheel;
    explicit RackVelocityGraph(Studio &owner) : studio(owner)
    {
        setComponentID("rack.velocityGraph");
        setTooltip("Graph | Left: edit | Ctrl: scale/transpose | Right: ramp | Shift: fine | Alt: reset | "
                   "Escape: cancel");
        propertyPicker.setComponentID("rack.graphProperty");
        propertyPicker.addItem("Velocity", 1);
        propertyPicker.addItem("Pitch", 2);
        propertyPicker.addItem("Pan (local sampler)", 3);
        propertyPicker.addItem("Fine pitch (local sampler)", 4);
        propertyPicker.addItem("Shift", 5);
        propertyPicker.addItem("Repeat", 6);
        propertyPicker.addItem("Release velocity (MIDI)", 7);
        propertyPicker.addItem("Mod X (sampler cutoff)", 8);
        propertyPicker.addItem("Mod Y (sampler resonance)", 9);
        propertyPicker.setSelectedId(propertyId(studio.rackGraphProperty), juce::dontSendNotification);
        propertyPicker.onChange = [this]
        {
            const auto outcome = studio.setRackGraphProperty(propertyFor(propertyPicker.getSelectedId()));
            if (outcome.failed())
                studio.status = outcome.getErrorMessage();
            propertyPicker.setSelectedId(propertyId(studio.rackGraphProperty), juce::dontSendNotification);
        };
        addAndMakeVisible(propertyPicker);
    }
    void resized() override
    {
        propertyPicker.setBounds(12, 3, std::max(60, left - 24), 25);
    }
    bool ownsGesture() const
    {
        return active && studio.controller.hasGesture() && studio.controller.gestureSerial() == serial &&
               studio.controller.projectIdentity == session && studio.controller.gestureOwner().isEmpty();
    }
    juce::Result finish(bool commit)
    {
        const bool owned = ownsGesture();
        if (owned)
        {
            const auto result = commit ? studio.controller.endGestureChecked(
                                             gestureProperty == "release"  ? "Edit step release velocity"
                                             : gestureProperty == "repeat" ? "Edit step Repeat"
                                             : gestureProperty == "shift"  ? "Edit step Shift"
                                             : gestureProperty == "pan"    ? "Edit step pan"
                                             : gestureProperty == "fine"   ? "Edit step fine pitch"
                                             : gestureProperty == "modX"   ? "Edit step Mod X"
                                             : gestureProperty == "modY"   ? "Edit step Mod Y"
                                             : gestureProperty == "pitch"  ? "Edit step pitches"
                                                                           : "Edit step velocities")
                                       : studio.controller.cancelGestureChecked();
            if (result.failed())
            {
                studio.status = result.getErrorMessage();
                studio.repaint();
                return result; // Keep serial, targets and baseline for retry or Escape.
            }
        }
        active = false;
        targets.clear();
        repaint();
        return juce::Result::ok();
    }
    void sync()
    {
        if (active &&
            (!ownsGesture() || patternId != studio.selectedPattern || channelId != studio.selectedChannel))
            finish(true);
        propertyPicker.setSelectedId(propertyId(studio.rackGraphProperty), juce::dontSendNotification);
        repaint();
    }
    juce::Rectangle<float> chart() const
    {
        return {float(left), 23.f, float(std::max(1, getWidth() - left - 8)),
                float(std::max(1, getHeight() - 34))};
    }
    juce::String repeatStepReadout(int step) const
    {
        const auto *pattern = studio.controller.project.pattern(studio.selectedPattern);
        const auto *channel = studio.controller.project.channel(studio.selectedChannel);
        int rate = -1;
        if (pattern && channel)
            for (const auto &note : pattern->notes)
                if (graphStep(note, channel->id, pattern->length) && int(std::llround(note.beat * 4)) == step)
                {
                    if (rate >= 0 && rate != note.repeatMode)
                        return "Step " + juce::String(step + 1) + ": Mixed rates";
                    rate = note.repeatMode;
                }
        const auto *info = noteRepeatRate(rate);
        return "Step " + juce::String(step + 1) + ": " + (info ? info->label : "No step");
    }
    void paint(juce::Graphics &g) override
    {
        g.fillAll(bg);
        g.setColour(grid.brighter(.15f));
        g.drawHorizontalLine(0, 8.f, float(getWidth() - 8));
        const auto *pattern = studio.controller.project.pattern(studio.selectedPattern);
        const auto *channel = studio.controller.project.channel(studio.selectedChannel);
        label(g, channel ? channel->name : "No Channel", {12, 29, left - 22, 20}, accent, 11);
        const bool pitch = studio.rackGraphProperty == "pitch";
        const auto *info = expression(studio.rackGraphProperty);
        const bool available = channel && supported(*channel, studio.rackGraphProperty);
        if (getHeight() >= 96)
        {
            if (studio.rackGraphProperty == "release")
                label(g,
                      !channel || channel->audioClip ? "No score note-off data"
                      : juce::String(noteReleaseAudioResponse(*channel)) == "ignored"
                          ? "Stored/export-only; audio ignores"
                          : "MIDI note-off / instrument dependent",
                      {12, 49, left - 22, 16}, muted, 10);
            else if (studio.rackGraphProperty == "repeat")
                label(g,
                      gestureProperty == "repeat" && patternId == studio.selectedPattern &&
                              channelId == studio.selectedChannel
                          ? repeatStepReadout(last)
                          : "Off .. 1/64 triplet",
                      {12, 49, left - 22, 16}, muted, 10);
            else if (studio.rackGraphProperty == "shift")
                label(g,
                      juce::String(stepShiftInfo()->minimum) + ".." + juce::String(stepShiftInfo()->maximum) +
                          " " + stepShiftInfo()->units + " / reset " +
                          juce::String(stepShiftInfo()->defaultValue),
                      {12, 49, left - 22, 16}, muted, 10);
            else if (pitch || info)
                label(g,
                      info ? juce::String(info->minimum) + ".." + juce::String(info->maximum) + " " +
                                 info->units + " / reset " + juce::String(info->defaultValue)
                           : "MIDI 0-127 / reset 60 (C5)",
                      {12, 49, left - 22, 16}, muted, 10);
            label(g,
                  pitch || info || studio.rackGraphProperty == "repeat" ? "Ctrl: offset / Right: ramp"
                                                                        : "Ctrl: scale / Right: ramp",
                  {12, getHeight() - 40, left - 22, 18}, muted, 10);
            label(g, "Alt: reset / Esc: cancel", {12, getHeight() - 22, left - 22, 18}, muted, 10);
        }
        else
            label(g, "Right: ramp / Alt: reset", {12, getHeight() - 21, left - 22, 18}, muted, 10);
        const auto area = chart();
        juce::Graphics::ScopedSaveState clip(g);
        g.reduceClipRegion(area.expanded(0, 20).toNearestInt());
        for (int step = offset; left + (step - offset) * width < getWidth() - 8; ++step)
        {
            if (!pattern || step * .25 >= pattern->length)
                break;
            const float x = float(left + (step - offset) * width);
            g.setColour((step / 4) % 2 ? grid.darker(.32f) : panel);
            g.fillRect(x, area.getY(), float(width - 2), area.getHeight());
            if (step % 4 == 0)
                label(g, juce::String(step + 1), {int(x), 3, int(width * 4), 17}, muted, 9);
        }
        if (info)
        {
            g.setColour(muted.withAlpha(.65f));
            g.drawHorizontalLine(juce::roundToInt(area.getCentreY()), area.getX(), area.getRight());
        }
        bool found = false;
        if (pattern && channel && !channel->audioClip)
            for (const auto &note : pattern->notes)
                if (graphStep(note, channel->id, pattern->length))
                {
                    found = true;
                    const int step = int(std::llround(note.beat * 4));
                    const float x = float(left + (step - offset) * width);
                    if (step < offset || x >= area.getRight())
                        continue;
                    g.setColour(accent.withAlpha(note.muted ? .3f : .85f));
                    const float level = normalized(note, studio.rackGraphProperty);
                    const float y = area.getBottom() - level * area.getHeight();
                    const float origin = info ? area.getCentreY() : area.getBottom();
                    const float height = std::max(2.f, std::abs(origin - y));
                    const float top = info ? (std::abs(origin - y) < 2.f ? origin - 1.f : std::min(origin, y))
                                           : area.getBottom() - height;
                    g.fillRoundedRectangle(x + 1, top, float(width - 4), height, 1.5f);
                }
        if (info && !available)
            label(g, "Stored only: native/VST3 and Audio Clip edits unavailable", area.toNearestInt(), muted,
                  11);
        else if (!found)
            label(g, channel && channel->audioClip ? "Audio Clip: no step events" : "Add steps to edit here",
                  area.toNearestInt(), muted, 11);
    }
    void mouseDown(const juce::MouseEvent &event) override
    {
        if (focusRack)
            focusRack();
        if (!chart().contains(event.position))
            return;
        if (active)
        {
            finish(true);
            if (active)
                return; // A refused completion must not overwrite the retained owner.
        }
        if (const auto available = studio.stepVelocityAdmission(); available.failed())
        {
            studio.status = available.getErrorMessage();
            studio.repaint();
            return;
        }
        const auto *pattern = studio.controller.project.pattern(studio.selectedPattern);
        const auto *channel = studio.controller.project.channel(studio.selectedChannel);
        if (!pattern || !channel)
            return;
        if (!supported(*channel, studio.rackGraphProperty))
        {
            studio.status =
                studio.rackGraphProperty == "modX" || studio.rackGraphProperty == "modY"
                    ? "stepExpression.unsupportedTarget: Mod X/Y editing requires a local sampler Channel"
                : expression(studio.rackGraphProperty)
                    ? "stepExpression.unsupportedTarget: Pan/fine editing requires a local sampler Channel"
                    : "Audio Clip: no step events";
            studio.repaint();
            return;
        }
        first = last = column(event.position);
        fine = event.mods.isShiftDown();
        fineStartY = event.position.y;
        const auto *info = expression(studio.rackGraphProperty);
        fineStartValue = studio.rackGraphProperty == "release"
                             ? float((stepReleaseInfo()->defaultValue - stepReleaseInfo()->minimum) /
                                     (stepReleaseInfo()->maximum - stepReleaseInfo()->minimum))
                         : studio.rackGraphProperty == "repeat"
                             ? float((stepRepeatInfo()->defaultValue - stepRepeatInfo()->minimum) /
                                     (stepRepeatInfo()->maximum - stepRepeatInfo()->minimum))
                         : studio.rackGraphProperty == "shift"
                             ? float((stepShiftInfo()->defaultValue - stepShiftInfo()->minimum) /
                                     (stepShiftInfo()->maximum - stepShiftInfo()->minimum))
                         : info
                             ? float((info->defaultValue - info->minimum) / (info->maximum - info->minimum))
                         : studio.rackGraphProperty == "pitch" ? 60.f / 127
                                                               : .8f;
        targets.clear();
        anchor = 0;
        for (const auto &note : pattern->notes)
            if (graphStep(note, channel->id, pattern->length))
            {
                targets.push_back(note.id);
                if (std::abs(note.beat - first * .25) < 1.e-8 && anchor == 0)
                {
                    anchor = note.id;
                    fineStartValue = normalized(note, studio.rackGraphProperty);
                }
            }
        firstValue = lastValue = value(event.position);
        mode = event.mods.isAltDown()           ? "reset"
               : event.mods.isCtrlDown()        ? "scale"
               : event.mods.isRightButtonDown() ? "ramp"
                                                : "set";
        if (targets.empty() || (mode == "scale" && anchor == 0))
            return;
        patternId = pattern->id;
        channelId = channel->id;
        gestureProperty = studio.rackGraphProperty;
        session = studio.controller.projectIdentity;
        if (!studio.controller.beginGesture())
            return;
        serial = studio.controller.gestureSerial();
        active = true;
        apply(event.position);
    }
    void mouseMove(const juce::MouseEvent &event) override
    {
        const juce::String help = "Graph | Left: edit | Ctrl: scale/transpose | Right: ramp | Shift: fine | "
                                  "Alt: reset | Escape: cancel";
        setTooltip(
            studio.rackGraphProperty == "modX" || studio.rackGraphProperty == "modY"
                ? juce::String(studio.rackGraphProperty == "modX"
                                   ? "Mod X: local sampler cutoff offset. "
                                   : "Mod Y: local sampler resonance offset. ") +
                      "Normalized0..1 / neutral0.5. Original composition: clamp(global + 2*(note-0.5),0,1). "
                      "Filter Off bypasses modulation; native/VST3 unsupported. Source state is not a DSP "
                      "ACK. | " +
                      help
            : studio.rackGraphProperty == "release"
                ? "Release velocity: normalized MIDI note-off data, not ADSR seconds. Local sampler/current "
                  "native Synth audio ignore it; third-party response is instrument dependent. | " +
                      help
            : studio.rackGraphProperty == "repeat" && chart().contains(event.position)
                ? repeatStepReadout(column(event.position)) + " | " + help
                : help);
    }
    void mouseDrag(const juce::MouseEvent &event) override
    {
        if (active)
            apply(event.position);
    }
    void mouseUp(const juce::MouseEvent &) override
    {
        finish(true);
    }
    void mouseWheelMove(const juce::MouseEvent &event, const juce::MouseWheelDetails &details) override
    {
        finish(true);
        if (!active && wheel)
            wheel(event, details);
    }
    void visibilityChanged() override
    {
        if (!isShowing())
            finish(true);
    }
};
} // namespace vibera
