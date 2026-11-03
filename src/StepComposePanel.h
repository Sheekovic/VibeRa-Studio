// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Theme.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <functional>
#include <vector>

namespace vibera
{
// Message-thread view of the shared application session. No private musical
// preview, transport owner, timer, or history is maintained by this component.
class StepComposePanel final : public juce::Component
{
  public:
    using Read = std::function<juce::var()>;
    using Act = std::function<juce::Result(const juce::var &)>;
    using Close = std::function<void()>;

    StepComposePanel(Read read, Act act, Close close)
        : readState(std::move(read)), act(std::move(act)), close(std::move(close))
    {
        setComponentID("stepComposePanel");
        setWantsKeyboardFocus(true);
        title.setText("Advanced Fill", juce::dontSendNotification);
        title.setFont(juce::Font(vibera::uiFont, 20.f, juce::Font::bold));
        title.setColour(juce::Label::textColourId, ink);
        addAndMakeVisible(title);
        channel.setComponentID("stepCompose.channel");
        channel.setTooltip("Select a Channel's retained draft. Other Channel drafts stay in this session.");
        channel.onChange = [this]
        {
            const int index = channel.getSelectedItemIndex();
            if (!updating && juce::isPositiveAndBelow(index, int(channelIds.size())))
            {
                auto request = object("select");
                put(request, "channel", channelIds[size_t(index)]);
                perform(request);
            }
        };
        addAndMakeVisible(channel);
        mode.setComponentID("stepCompose.mode");
        mode.addItem("Manual", 1);
        mode.addItem("Every N steps", 2);
        mode.addItem("Euclidean", 3);
        mode.onChange = [this]
        {
            if (!updating)
                update("mode", mode.getSelectedId() == 2   ? "every"
                               : mode.getSelectedId() == 3 ? "euclidean"
                                                           : "manual");
        };
        addAndMakeVisible(mode);
        setupNumber(steps, "steps", 1, 4096,
                    "Sequence length in sixteenth-note steps (1–4096). One step =0.25 quarter-note beats.");
        setupNumber(every, "every", 1, 4096, "Every-N spacing in steps, bounded by sequence length.");
        setupNumber(pulses, "pulses", 0, 4096,
                    "Euclidean pulse count (0 through sequence length). Zero produces an empty mask.");
        setupNumber(rotation, "rotation", -4096, 4096,
                    "Rotate by signed whole steps (-4096–4096), normalized by the shared recipe service.");
        setupNumber(seed, "seed", 0, 4294967295.,
                    "Deterministic unsigned32 seed (0–4294967295). Retained with this Channel draft.");
        steps.onValueChange = [this]
        {
            if (updating)
                return;
            auto recipe = writableRecipe();
            if (!recipe.isObject())
                return;
            const int count = int(steps.getValue());
            put(recipe, "sequenceSteps", count);
            put(recipe, "every", juce::jlimit(1, count, int(recipe["every"])));
            put(recipe, "pulses", juce::jlimit(0, count, int(recipe["pulses"])));
            juce::Array<juce::var> manual;
            const auto *old = recipe["manual"].getArray();
            for (int i = 0; i < count; ++i)
                manual.add(old && i < old->size() ? (*old)[i] : juce::var(0));
            put(recipe, "manual", manual);
            submitRecipe(recipe);
        };
        every.onValueChange = [this]
        {
            if (!updating)
                update("every", int(every.getValue()));
        };
        pulses.onValueChange = [this]
        {
            if (!updating)
                update("pulses", int(pulses.getValue()));
        };
        rotation.onValueChange = [this]
        {
            if (!updating)
                update("rotation", int(rotation.getValue()));
        };
        seed.onValueChange = [this]
        {
            if (!updating)
                update("seed", juce::int64(seed.getValue()));
        };
        setupToggle(
            dice, "dice", "Dice (seeded)",
            "Use the shared deterministic seeded Dice recipe; no random state is generated in this view.");
        setupToggle(loop, "loop", "Loop sequence",
                    "On repeats this Channel cycle through the Pattern. Off writes one cycle; steps outside "
                    "a Manual Pattern window remain dormant.");
        setupToggle(convert, "convertMelodic", "Allow replacing melodic notes",
                    "Explicit consent to replace this Channel's melodic score with steps. Off preserves the "
                    "shared service's refusal.");
        dice.onClick = [this]
        {
            if (!updating)
                update("dice", dice.getToggleState());
        };
        loop.onClick = [this]
        {
            if (!updating)
                update("loop", loop.getToggleState());
        };
        convert.onClick = [this]
        {
            if (!updating)
                update("convertMelodic", convert.getToggleState());
        };
        preview.setComponentID("stepCompose.preview");
        preview.setButtonText("Preview");
        preview.setTooltip("Off edits drafts only. On asks the shared application to publish "
                           "baseline-relative preview changes; Cancel restores its owned baseline.");
        preview.onClick = [this]
        {
            if (!updating)
            {
                auto request = object("preview");
                put(request, "enabled", preview.getToggleState());
                perform(request);
            }
        };
        addAndMakeVisible(preview);
        setupToggle(solo, "solo", "Solo",
                    "Temporarily hear this Channel alone. Accept and Cancel restore the opening mute masks.");
        solo.onClick = [this]
        {
            if (!updating)
            {
                auto request = object("solo");
                put(request, "enabled", solo.getToggleState());
                perform(request);
            }
        };
        setupButton(play, "play", "Play", [this] { perform(object("play")); });
        setupButton(reroll, "reroll", "Dice", [this] { perform(object("dice")); });
        reroll.setTooltip("Advance the shared deterministic seed and generate another Dice mask.");
        setupButton(manual, "manual", "Manual", [this] { perform(object("manual")); });
        manual.setTooltip(
            "Keep the final displayed hits as Manual; clear Dice and rotation without changing hits.");
        setupButton(reset, "reset", "Reset Channel",
                    [this]
                    {
                        auto request = object("reset");
                        put(request, "all", false);
                        perform(request);
                    });
        setupButton(resetAll, "resetAll", "Reset all drafts",
                    [this]
                    {
                        auto request = object("reset");
                        put(request, "all", true);
                        perform(request);
                    });
        reset.setTooltip("Restore this Channel's opening draft, not an empty pattern.");
        resetAll.setTooltip("Restore every Channel's opening draft in this session.");
        setupButton(accept, "accept", "Accept", [this] { perform(object("commit")); });
        setupButton(cancel, "cancel", "Cancel", [this] { perform(object("cancel")); });
        accept.addShortcut(juce::KeyPress(juce::KeyPress::returnKey));
        cancel.addShortcut(juce::KeyPress(juce::KeyPress::escapeKey));
        status.setComponentID("stepCompose.status");
        status.setFont(juce::Font(12.f));
        status.setMinimumHorizontalScale(.9f);
        status.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(status);
        gridHint.setText(
            "Left-drag paints steps; right-drag erases. Editing freezes the final mask as Manual.",
            juce::dontSendNotification);
        gridHint.setColour(juce::Label::textColourId, muted);
        gridHint.setFont(juce::Font(12.f));
        addAndMakeVisible(gridHint);
        mask.setComponentID("stepCompose.mask");
        mask.setTooltip(
            "Sixteenth-note cells. Four steps form one quarter-note beat. Left paints, right erases; "
            "drag to edit the final mask as Manual (clears Dice and rotation).");
        auto paintCell = [this](int cell, bool enabled)
        {
            auto recipe = writableRecipe();
            const auto *output = selectedRecipe()["mask"].getArray();
            if (!recipe.isObject() || !output || !juce::isPositiveAndBelow(cell, output->size()))
                return;
            auto manual = *output;
            manual.set(cell, enabled ? 1 : 0);
            put(recipe, "manual", manual);
            put(recipe, "mode", "manual");
            put(recipe, "dice", false);
            put(recipe, "rotation", 0);
            submitRecipe(recipe);
        };
        mask.onCell = paintCell;
        ring.onCell = std::move(paintCell);
        ring.setComponentID("stepCompose.ring");
        ring.setTooltip("One light per actual step, starting at the top and moving clockwise. "
                        "Left paints, right erases. The centre knob rotates by whole steps.");
        ring.onRotation = [this](int value)
        {
            if (!updating)
                update("rotation", value);
        };
        addAndMakeVisible(ring);
        viewport.setComponentID("stepCompose.viewport");
        viewport.setViewedComponent(&mask, false);
        viewport.setScrollBarsShown(true, false);
        addAndMakeVisible(viewport);
        static constexpr std::array<const char *, 6> captions{"Steps",  "Mode",     "Every N",
                                                              "Pulses", "Rotation", "Seed"};
        for (size_t i = 0; i < captions.size(); ++i)
        {
            labels[i].setText(captions[i], juce::dontSendNotification);
            labels[i].setColour(juce::Label::textColourId, muted);
            labels[i].setFont(juce::Font(12.f));
            addAndMakeVisible(labels[i]);
        }
        setSize(820, 560);
        refresh(); // Reading/opening never submits an action or enables Preview.
    }

    ~StepComposePanel() override
    {
        viewport.setViewedComponent(nullptr, false);
    }

    void refresh()
    {
        juce::Component::SafePointer<StepComposePanel> safe(this);
        auto read = readState;
        juce::var next;
        try
        {
            if (read)
                next = read();
        }
        catch (const std::exception &error)
        {
            if (safe)
                safe->lastError = error.what();
        }
        catch (...)
        {
            if (safe)
                safe->lastError = "The shared Fill session could not be read.";
        }
        if (!safe)
            return;
        // Timer/readback refresh must not rebuild an open Channel popup or
        // overwrite a numeric text edit when no session state has changed.
        const auto identity = juce::JSON::toString(next);
        if (identity == renderedState && lastError == renderedError)
            return;
        renderedState = identity;
        renderedError = lastError;
        const juce::ScopedValueSetter<bool> guard(updating, true);
        state = next;
        channel.clear(juce::dontSendNotification);
        channelIds.clear();
        if (const auto *recipes = state["recipes"].getArray())
            for (const auto &recipe : *recipes)
            {
                channelIds.push_back(juce::int64(recipe["channel"]));
                auto name = recipe["name"].toString();
                channel.addItem(name.isNotEmpty() ? name : "Channel " + recipe["channel"].toString(),
                                int(channelIds.size()));
            }
        const auto active = juce::int64(state["activeChannel"]);
        const auto found = std::find(channelIds.begin(), channelIds.end(), active);
        channel.setSelectedItemIndex(found == channelIds.end() ? -1 : int(found - channelIds.begin()),
                                     juce::dontSendNotification);
        const auto recipe = selectedRecipe();
        const int count = juce::jlimit(1, 4096, int(recipe["sequenceSteps"]));
        steps.setValue(count, juce::dontSendNotification);
        every.setRange(1, count, 1);
        pulses.setRange(0, count, 1);
        every.setValue(int(recipe["every"]), juce::dontSendNotification);
        pulses.setValue(int(recipe["pulses"]), juce::dontSendNotification);
        rotation.setValue(int(recipe["rotation"]), juce::dontSendNotification);
        seed.setValue(double(recipe["seed"]), juce::dontSendNotification);
        const auto modeName = recipe["mode"].toString();
        mode.setSelectedId(modeName == "every"       ? 2
                           : modeName == "euclidean" ? 3
                                                     : 1,
                           juce::dontSendNotification);
        dice.setToggleState(bool(recipe["dice"]), juce::dontSendNotification);
        loop.setToggleState(bool(recipe["loop"]), juce::dontSendNotification);
        convert.setToggleState(bool(recipe["convertMelodic"]), juce::dontSendNotification);
        preview.setToggleState(bool(state["preview"]), juce::dontSendNotification);
        solo.setToggleState(bool(state["solo"]), juce::dontSendNotification);
        play.setButtonText(bool(state["playing"]) ? "Pause" : "Play");
        const bool pending = state["completionState"].toString() == "pending";
        const bool editable = pending && recipe.isObject();
        for (auto *control :
             std::array<juce::Component *, 14>{&steps, &mode, &rotation, &seed, &dice, &loop, &convert,
                                               &reset, &mask, &preview, &solo, &reroll, &manual, &ring})
            control->setEnabled(editable);
        every.setEnabled(editable && modeName == "every");
        pulses.setEnabled(editable && modeName == "euclidean");
        channel.setEnabled(pending && !channelIds.empty());
        resetAll.setEnabled(pending && !channelIds.empty());
        play.setEnabled(pending);
        accept.setEnabled(pending && recipe.isObject());
        cancel.setEnabled(pending);
        mask.setMask(recipe["mask"], active);
        ring.setMask(recipe["mask"], active);
        ring.setRotation(int(recipe["rotation"]));
        ring.setVisible(count <= 64 && recipe.isObject());
        gridHint.setText(
            count > 64
                ? juce::String(count) +
                      " steps: use the full grid below (ring supports up to 64). Left paints; right erases."
                : "Left-drag paints; right-drag erases. Four steps = one beat. Editing freezes the final "
                  "mask as Manual.",
            juce::dontSendNotification);
        auto error = state["error"]["message"].toString();
        if (error.isNotEmpty())
            error = state["error"]["code"].toString() + ": " + error;
        if (lastError.isNotEmpty())
            error = lastError;
        status.setColour(juce::Label::textColourId, error.isNotEmpty() ? orange : muted);
        status.setText(
            error.isNotEmpty() ? error
            : pending
                ? (bool(state["preview"])
                       ? "Preview is on. Accept keeps this session's edits; Cancel restores its baseline."
                       : "Draft only. Enable Preview to hear the prepared result; no changes are committed "
                         "by opening this panel.")
                : "Session " + state["completionState"].toString(),
            juce::dontSendNotification);
        status.setTooltip(status.getText());
        resized();
        repaint();
    }

    bool keyPressed(const juce::KeyPress &key) override
    {
        if (key.getKeyCode() == juce::KeyPress::escapeKey && cancel.isEnabled())
        {
            perform(object("cancel"));
            return true;
        }
        if (key.getKeyCode() == juce::KeyPress::returnKey && accept.isEnabled())
        {
            perform(object("commit"));
            return true;
        }
        return false;
    }

    void paint(juce::Graphics &g) override
    {
        g.fillAll(bg);
        g.setColour(panel);
        g.fillRoundedRectangle(getLocalBounds().reduced(8).toFloat(), 8.f);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(16);
        title.setBounds(area.removeFromTop(28));
        auto channelRow = area.removeFromTop(30);
        play.setBounds(channelRow.removeFromRight(90).reduced(2, 0));
        preview.setBounds(channelRow.removeFromRight(110));
        solo.setBounds(channelRow.removeFromRight(76));
        channel.setBounds(channelRow.reduced(0, 1));
        area.removeFromTop(6);
        auto footer = area.removeFromBottom(32);
        cancel.setBounds(footer.removeFromRight(88).reduced(2, 0));
        accept.setBounds(footer.removeFromRight(88).reduced(2, 0));
        reset.setBounds(footer.removeFromLeft(120).reduced(2, 0));
        resetAll.setBounds(footer.removeFromLeft(128).reduced(2, 0));
        reroll.setBounds(footer.removeFromLeft(82).reduced(2, 0));
        manual.setBounds(footer.removeFromLeft(82).reduced(2, 0));
        status.setBounds(area.removeFromBottom(38));
        const bool showRing = ring.isVisible();
        auto controls = area.removeFromTop(showRing ? 218 : 154);
        if (showRing)
        {
            ring.setBounds(controls.removeFromLeft(218).reduced(0, 2));
            controls.removeFromLeft(10);
        }
        const int columns = showRing ? 2 : 3;
        const int columnWidth = controls.getWidth() / columns;
        std::array<juce::Component *, 6> fields{&steps, &mode, &every, &pulses, &rotation, &seed};
        for (int row = 0; row < 6 / columns; ++row)
        {
            auto line = controls.removeFromTop(48);
            for (int column = 0; column < columns; ++column)
            {
                const auto index = size_t(row * columns + column);
                auto cell =
                    line.removeFromLeft(column == columns - 1 ? line.getWidth() : columnWidth).reduced(3, 0);
                labels[index].setBounds(cell.removeFromTop(18));
                fields[index]->setBounds(cell.removeFromTop(28));
            }
        }
        auto flags = controls.removeFromTop(28);
        dice.setBounds(flags.removeFromLeft(flags.getWidth() / 2));
        loop.setBounds(flags);
        convert.setBounds(controls.removeFromTop(28));
        gridHint.setBounds(area.removeFromTop(20));
        viewport.setBounds(area.reduced(0, 3));
        mask.setSize(std::max(1, viewport.getWidth() - scrollBarThickness - 2), mask.contentHeight());
    }

  private:
    // Both views edit the same final mask. Each stroke visits a cell only once;
    // refreshes may update its colour, but cannot retarget it to another Channel.
    class MaskSurface : public juce::Component, public juce::SettableTooltipClient
    {
      public:
        std::function<void(int, bool)> onCell;
        void setMask(const juce::var &values, juce::int64 channel)
        {
            const auto *array = values.getArray();
            const auto count = array ? std::min(4096, array->size()) : 0;
            if (channel != currentChannel || count != int(cells.size()))
                painting = false;
            currentChannel = channel;
            cells.clear();
            if (array)
                for (int i = 0; i < count; ++i)
                    cells.push_back(int((*array)[i]) != 0);
            repaint();
        }
        void mouseDown(const juce::MouseEvent &event) override
        {
            painting = false;
            if (!isEnabled() || (!event.mods.isLeftButtonDown() && !event.mods.isRightButtonDown()) ||
                cellAt(event.position) < 0)
                return;
            visited.fill(false);
            painting = true;
            paintValue = !event.mods.isRightButtonDown();
            lastPoint = event.position;
            paintTo(event.position);
        }
        void mouseDrag(const juce::MouseEvent &event) override
        {
            if (painting && isEnabled())
                paintTo(event.position);
        }
        void mouseUp(const juce::MouseEvent &) override
        {
            painting = false;
        }

      protected:
        std::vector<bool> cells;
        virtual int cellAt(juce::Point<float>) const = 0;

      private:
        void paintTo(juce::Point<float> point)
        {
            // Bound interpolation to this actual surface, even if the captured
            // pointer is dragged far outside its window. Gaps remain gaps.
            point.x = juce::jlimit(-1.f, float(getWidth()), point.x);
            point.y = juce::jlimit(-1.f, float(getHeight()), point.y);
            const auto start = lastPoint;
            lastPoint = point;
            const int samples = std::max(1, int(std::ceil(start.getDistanceFrom(point) / 3.f)));
            juce::Component::SafePointer<MaskSurface> safe(this);
            for (int sample = 1; sample <= samples; ++sample)
            {
                const auto position = start + (point - start) * (float(sample) / float(samples));
                const int cell = cellAt(position);
                if (!juce::isPositiveAndBelow(cell, int(cells.size())) || visited[size_t(cell)])
                    continue;
                visited[size_t(cell)] = true;
                auto callback = onCell;
                if (callback)
                    callback(cell, paintValue);
                if (!safe || !safe->painting || !safe->isEnabled())
                    return;
            }
        }
        juce::int64 currentChannel = 0;
        juce::Point<float> lastPoint;
        std::array<bool, 4096> visited{};
        bool painting = false, paintValue = true;
    };

    class MaskGrid final : public MaskSurface
    {
      public:
        int contentHeight() const
        {
            return std::max(1, (int(cells.size()) + 15) / 16) * rowHeight;
        }
        void paint(juce::Graphics &g) override
        {
            g.fillAll(bg);
            const float width = getWidth() / 16.f;
            const auto clip = g.getClipBounds();
            const int first = std::max(0, clip.getY() / rowHeight);
            const int last = std::min((int(cells.size()) + 15) / 16, clip.getBottom() / rowHeight + 1);
            for (int row = first; row < last; ++row)
                for (int column = 0; column < 16; ++column)
                {
                    const int index = row * 16 + column;
                    if (index >= int(cells.size()))
                        break;
                    const float x = column * width;
                    if (column % 4 == 0)
                    {
                        g.setColour(muted);
                        g.setFont(11.f);
                        g.drawText("Beat " + juce::String(index / 4 + 1), int(x), row * rowHeight,
                                   int(width * 4), 18, juce::Justification::centredLeft);
                    }
                    const auto bounds =
                        juce::Rectangle<float>(x + 2, float(row * rowHeight + 19), width - 4, 27);
                    g.setColour(cells[size_t(index)] ? accent : grid);
                    g.fillRoundedRectangle(bounds, 3.f);
                    g.setColour(cells[size_t(index)] ? bg : ink);
                    g.setFont(12.f);
                    g.drawText(juce::String(index + 1), bounds.toNearestInt(), juce::Justification::centred);
                }
        }

      private:
        int cellAt(juce::Point<float> point) const override
        {
            if (getWidth() <= 0 || point.x < 0 || point.x >= float(getWidth()) || point.y < 0 ||
                point.y >= float(getHeight()))
                return -1;
            const int row = int(point.y) / rowHeight;
            const int y = int(point.y) % rowHeight;
            if (y < 19 || y >= 46)
                return -1;
            const int cell = row * 16 + juce::jlimit(0, 15, int(point.x * 16 / float(getWidth())));
            return cell < int(cells.size()) ? cell : -1;
        }
        static constexpr int rowHeight = 52;
    };

    class StepRing final : public MaskSurface
    {
      public:
        std::function<void(int)> onRotation;
        StepRing()
        {
            knob.setComponentID("stepCompose.rotationKnob");
            knob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
            knob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 72, 20);
            knob.setNumDecimalPlacesToDisplay(0);
            knob.setTooltip(
                "Clockwise rotation in whole steps. The signed Rotation field shows the shared value.");
            knob.onValueChange = [this]
            {
                auto callback = onRotation;
                if (callback)
                    callback(int(knob.getValue()));
            };
            addAndMakeVisible(knob);
        }
        void setRotation(int value)
        {
            const int count = std::max(1, int(cells.size()));
            knob.setRange(0, std::max(1, count - 1), 1);
            knob.setValue((value % count + count) % count, juce::dontSendNotification);
            knob.setEnabled(isEnabled() && count > 1);
        }
        void paint(juce::Graphics &g) override
        {
            const auto centre = getLocalBounds().toFloat().getCentre();
            const float radius = ringRadius();
            g.setColour(bg);
            g.fillEllipse(centre.x - radius - 12, centre.y - radius - 12, (radius + 12) * 2,
                          (radius + 12) * 2);
            g.setColour(grid);
            g.drawEllipse(centre.x - radius, centre.y - radius, radius * 2, radius * 2, 1.f);
            const float size = cellSize();
            for (int i = 0; i < int(cells.size()); ++i)
            {
                const auto point = cellCentre(i);
                g.setColour(cells[size_t(i)] ? accent : grid.brighter(.15f));
                g.fillEllipse(point.x - size / 2, point.y - size / 2, size, size);
                if (cells.size() <= 16)
                {
                    g.setColour(cells[size_t(i)] ? bg : ink);
                    g.setFont(10.f);
                    g.drawText(juce::String(i + 1),
                               juce::Rectangle<float>(point.x - size / 2, point.y - size / 2, size, size)
                                   .toNearestInt(),
                               juce::Justification::centred);
                }
            }
            g.setColour(orange);
            juce::Path marker;
            const float top = centre.y - radius - size / 2 - 2;
            marker.addTriangle(centre.x - 4, top - 6, centre.x + 4, top - 6, centre.x, top);
            g.fillPath(marker);
            g.setColour(muted);
            g.setFont(11.f);
            g.drawText("ROTATE", int(centre.x - 36), int(centre.y - 51), 72, 16,
                       juce::Justification::centred);
            g.drawText(juce::String(cells.size()) + " steps", int(centre.x - 40), int(centre.y + 48), 80, 16,
                       juce::Justification::centred);
        }
        void resized() override
        {
            const auto centre = getLocalBounds().getCentre();
            knob.setBounds(centre.x - 40, centre.y - 35, 80, 82);
        }

      private:
        float ringRadius() const
        {
            return std::max(1.f, std::min(getWidth(), getHeight()) * .5f - 20.f);
        }
        float cellSize() const
        {
            return juce::jlimit(5.f, 18.f,
                                2.f * juce::MathConstants<float>::pi * ringRadius() /
                                    float(std::max(size_t(1), cells.size())) * .74f);
        }
        juce::Point<float> cellCentre(int cell) const
        {
            const float angle =
                juce::MathConstants<float>::twoPi * float(cell) / float(std::max(size_t(1), cells.size())) -
                juce::MathConstants<float>::halfPi;
            const auto centre = getLocalBounds().toFloat().getCentre();
            return centre + juce::Point<float>(std::cos(angle), std::sin(angle)) * ringRadius();
        }
        int cellAt(juce::Point<float> point) const override
        {
            if (cells.size() > 64)
                return -1;
            for (int i = 0; i < int(cells.size()); ++i)
                if (point.getDistanceFrom(cellCentre(i)) <= cellSize() * .5f + 1.f)
                    return i;
            return -1;
        }
        juce::Slider knob;
    };

    static juce::var object(const char *action)
    {
        auto result = juce::var(new juce::DynamicObject);
        put(result, "action", action);
        return result;
    }
    static void put(juce::var &object, const char *name, juce::var value)
    {
        object.getDynamicObject()->setProperty(name, std::move(value));
    }
    void setupNumber(juce::Slider &slider, const char *id, double minimum, double maximum,
                     const char *tooltip)
    {
        slider.setComponentID(juce::String("stepCompose.") + id);
        slider.setSliderStyle(juce::Slider::IncDecButtons);
        slider.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 108, 26);
        slider.setRange(minimum, maximum, 1);
        slider.setNumDecimalPlacesToDisplay(0);
        slider.setTooltip(tooltip);
        addAndMakeVisible(slider);
    }
    void setupToggle(juce::ToggleButton &button, const char *id, const char *caption, const char *tooltip)
    {
        button.setComponentID(juce::String("stepCompose.") + id);
        button.setButtonText(caption);
        button.setTooltip(tooltip);
        addAndMakeVisible(button);
    }
    void setupButton(juce::TextButton &button, const char *id, const char *caption,
                     std::function<void()> callback)
    {
        button.setComponentID(juce::String("stepCompose.") + id);
        button.setButtonText(caption);
        button.onClick = std::move(callback);
        addAndMakeVisible(button);
    }
    juce::var selectedRecipe() const
    {
        if (const auto *recipes = state["recipes"].getArray())
            for (const auto &recipe : *recipes)
                if (juce::int64(recipe["channel"]) == juce::int64(state["activeChannel"]))
                    return recipe;
        return {};
    }
    juce::var writableRecipe() const
    {
        const auto source = selectedRecipe();
        if (!source.isObject())
            return {};
        auto result = juce::var(new juce::DynamicObject);
        for (const auto *name : {"channel", "sequenceSteps", "mode", "every", "pulses", "rotation", "seed",
                                 "dice", "loop", "convertMelodic", "manual"})
            put(result, name, source[name].clone());
        return result;
    }
    void update(const char *field, juce::var value)
    {
        auto recipe = writableRecipe();
        if (!recipe.isObject())
            return;
        put(recipe, field, std::move(value));
        submitRecipe(recipe);
    }
    void submitRecipe(const juce::var &recipe)
    {
        auto request = object("update");
        put(request, "recipes", juce::Array<juce::var>{recipe});
        perform(request);
    }
    void perform(const juce::var &request)
    {
        if (updating || !act || state["completionState"].toString() != "pending")
            return;
        juce::Component::SafePointer<StepComposePanel> safe(this);
        auto invoke = act;
        auto outcome = juce::Result::fail("Fill action did not return");
        try
        {
            outcome = invoke(request);
        }
        catch (const std::exception &error)
        {
            outcome = juce::Result::fail(error.what());
        }
        catch (...)
        {
            outcome = juce::Result::fail("Shared Fill action failed");
        }
        if (!safe)
            return;
        lastError = outcome.failed() ? outcome.getErrorMessage() : juce::String{};
        refresh();
        if (!safe)
            return;
        const auto action = request["action"].toString();
        const auto terminal = state["completionState"].toString();
        if (outcome.wasOk() && ((action == "commit" && terminal == "completed") ||
                                (action == "cancel" && terminal == "cancelled")))
        {
            auto callback = close;
            if (callback)
                callback();
        }
    }

    Read readState;
    Act act;
    Close close;
    juce::var state;
    juce::String lastError, renderedState, renderedError;
    bool updating = false;
    std::vector<juce::int64> channelIds;
    juce::Label title, status, gridHint;
    std::array<juce::Label, 6> labels;
    juce::ComboBox channel, mode;
    juce::Slider steps, every, pulses, rotation, seed;
    juce::ToggleButton dice, loop, convert, preview, solo;
    juce::TextButton play, reset, resetAll, accept, cancel, reroll, manual;
    MaskGrid mask;
    StepRing ring;
    juce::Viewport viewport;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StepComposePanel)
};
} // namespace vibera
