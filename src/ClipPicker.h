// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Studio.h"

namespace vibera
{
inline bool pickerPayload(const juce::var &description, const juce::String &identity, CommandArgs &args)
{
    const auto *object = description.getDynamicObject();
    if (!object || !bool(object->getProperty("viberaPicker")) ||
        object->getProperty("project").toString() != identity)
        return false;
    const auto kind = object->getProperty("kind").toString();
    const auto *ids = object->getProperty("ids").getArray();
    if ((kind != "pattern" && kind != "audio" && kind != "automation") || !ids || ids->isEmpty() ||
        ids->size() > 128)
        return false;
    CommandArgs result;
    result.text = kind;
    for (const auto &id : *ids)
    {
        if ((!id.isInt() && !id.isInt64()) || juce::int64(id) <= 0)
            return false;
        result.ids.push_back(juce::int64(id));
    }
    args = std::move(result);
    return true;
}
class PickerDivider final : public juce::Component
{
    int origin = 0;

  public:
    std::function<void(int)> move;
    PickerDivider()
    {
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
    }
    void mouseDown(const juce::MouseEvent &) override
    {
        origin = getX();
    }
    void mouseDrag(const juce::MouseEvent &e) override
    {
        if (move)
            move(origin + e.getDistanceFromDragStartX());
    }
    void paint(juce::Graphics &g) override
    {
        g.setColour(isMouseOverOrDragging() ? accent : grid);
        g.fillRect(getWidth() / 2, 0, 1, getHeight());
    }
};
// Message-thread view of canonical source IDs. No decoding, DSP or file access in paint.
class ClipPicker final : public juce::Component, private juce::ListBoxModel
{
    Studio &studio;
    juce::ListBox list{"Clip sources", this};
    std::array<juce::TextButton, 3> tabs;
    juce::ComboBox groups;
    juce::TextEditor search;
    std::vector<Id> rows;
    juce::String filterGroup, identity;
    bool updating = false;
    int kind = 0;
    std::unique_ptr<juce::AlertWindow> dialog;

    juce::String name(Id id) const
    {
        const auto &project = studio.controller.project;
        if (kind == 0)
        {
            if (const auto *p = project.pattern(id))
                return p->name;
        }
        else if (kind == 1)
        {
            if (const auto *c = project.channel(id))
                return c->name;
        }
        else if (const auto *a = project.automationSource(id))
            return juce::String::fromUTF8(a->name.c_str());
        return {};
    }
    int getNumRows() override
    {
        return int(rows.size());
    }
    void paintListBoxItem(int row, juce::Graphics &g, int width, int height, bool selected) override
    {
        if (!juce::isPositiveAndBelow(row, int(rows.size())))
            return;
        const auto id = rows[size_t(row)];
        auto colour = accent;
        const auto &project = studio.controller.project;
        if (const auto *p = kind == 0 ? project.pattern(id) : nullptr)
            if (p->colour)
                colour = juce::Colour(p->colour);
        g.setColour(selected ? grid.brighter(.12f) : panel);
        g.fillRoundedRectangle(juce::Rectangle<float>(2, 2, float(width - 4), float(height - 4)), 3);
        g.setColour(colour);
        g.fillRoundedRectangle(4, 7, 3, float(height - 14), 1.5f);
        label(g, name(id), {12, 4, width - 20, 20}, selected ? ink : muted, 12);
        auto area = juce::Rectangle<float>(12, 28, float(std::max(1, width - 24)), 19);
        if (kind == 0)
        {
            if (const auto *p = project.pattern(id))
            {
                g.setColour(colour.withAlpha(.85f));
                // Same mini roll as the Playlist clips: a row per Channel for multi-Channel
                // Patterns, else the pitch range.
                int low = 127, high = 0;
                std::vector<Id> channels;
                for (const auto &n : p->notes)
                {
                    low = std::min(low, n.pitch);
                    high = std::max(high, n.pitch);
                    if (std::find(channels.begin(), channels.end(), n.channel) == channels.end())
                        channels.push_back(n.channel);
                }
                std::sort(channels.begin(), channels.end());
                const bool channelRows = channels.size() > 1;
                const int rows = channelRows ? int(channels.size()) : std::max(1, high - low + 1);
                const float rowHeight = juce::jlimit(2.f, 4.f, area.getHeight() / float(std::max(rows, 4)));
                const float span = rowHeight * float(rows);
                const float top = area.getY() + std::max(0.f, (area.getHeight() - span) * .5f);
                for (const auto &n : p->notes)
                    if (n.beat < p->length)
                    {
                        const auto x = area.getX() + float(n.beat / p->length) * area.getWidth();
                        const auto w = std::max(
                            2.f, float(std::min(n.length, p->length - n.beat) / p->length) * area.getWidth() - 1.f);
                        const int row = channelRows ? int(std::lower_bound(channels.begin(), channels.end(), n.channel) -
                                                          channels.begin())
                                                    : high - n.pitch;
                        const auto y = rows <= 1 ? top : top + float(row) * (span - rowHeight) / float(rows - 1);
                        g.fillRoundedRectangle(x, y, std::min(w, area.getRight() - x), std::max(2.f, rowHeight - 1.f), 1.f);
                    }
                if (p->notes.empty())
                    label(g, "Empty Pattern", area.toNearestInt(), muted, 10);
            }
        }
        else if (kind == 1)
        {
            if (const auto *c = project.channel(id))
                label(g, juce::String(c->audioDuration, 2) + " s  /  Mixer " + juce::String(c->mixer),
                      area.toNearestInt(), muted, 10);
        }
        else if (const auto *a = project.automationSource(id))
        {
            juce::Path curve;
            for (int x = 0; x <= int(area.getWidth()); ++x)
            {
                const auto y =
                    area.getBottom() -
                    automationSourceValueAt(*a, a->length * x / area.getWidth()) * area.getHeight();
                if (x == 0)
                    curve.startNewSubPath(area.getX(), y);
                else
                    curve.lineTo(area.getX() + x, y);
            }
            g.setColour(colour);
            g.strokePath(curve, juce::PathStrokeType(1.5f));
        }
    }
    void selectedRowsChanged(int last) override
    {
        if (!updating && juce::isPositiveAndBelow(last, int(rows.size())))
        {
            const auto id = rows[size_t(last)];
            if (kind == 0)
            {
                studio.selectPattern(id);
            }
            if (choose)
                choose(kind, id);
            studio.focus = Scope::playlist;
            studio.refresh();
        }
    }
    void listBoxItemDoubleClicked(int row, const juce::MouseEvent &) override
    {
        if (kind == 0 && juce::isPositiveAndBelow(row, int(rows.size())))
        {
            studio.selectedPattern = rows[size_t(row)];
            studio.showEditor(0);
            studio.refresh();
        }
    }
    juce::var getDragSourceDescription(const juce::SparseSet<int> &selected) override
    {
        juce::Array<juce::var> ids;
        for (int i = 0; i < selected.size(); ++i)
            if (juce::isPositiveAndBelow(selected[i], int(rows.size())))
                ids.add(juce::int64(rows[size_t(selected[i])]));
        auto *object = new juce::DynamicObject;
        object->setProperty("viberaPicker", true);
        object->setProperty("kind", sourceKind());
        object->setProperty("project", studio.controller.projectIdentity);
        object->setProperty("ids", ids);
        return object;
    }
    void listBoxItemClicked(int row, const juce::MouseEvent &event) override
    {
        if (!event.mods.isPopupMenu() || !juce::isPositiveAndBelow(row, int(rows.size())))
            return;
        if (!list.isRowSelected(row))
            list.selectRow(row);
        if (kind == 2)
        {
            const auto ids = selectedIds();
            auto menu = automationSourceMenu(ids);
            menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&list),
                               [safe = juce::Component::SafePointer<ClipPicker>(this), ids,
                                project = studio.controller.projectIdentity,
                                revision = studio.controller.revision](int action)
                               {
                                   if (!safe || !action || ids.size() != 1 || safe->kind != 2 ||
                                       safe->studio.controller.projectIdentity != project ||
                                       safe->studio.controller.revision != revision)
                                       return;
                                   safe->cloneAutomationSource(ids.front(), action == 2);
                               });
            return;
        }
        if (kind != 0)
            return;
        const auto ids = selectedIds();
        juce::PopupMenu menu;
        menu.addItem(1, "Group selected...");
        menu.addItem(2, "Ungroup selected");
        menu.addSeparator();
        menu.addItem(3, "Rename / colour...", ids.size() == 1);
        menu.addItem(4, "Transpose...", ids.size() == 1);
        menu.addItem(5, "Open Channel Rack", ids.size() == 1);
        menu.addSeparator();
        menu.addItem(6, ids.size() > 1 ? "Clone selected Patterns" : "Clone Pattern");
        menu.addItem(7, ids.size() > 1 ? "Delete selected Patterns..." : "Delete Pattern...");
        menu.addItem(8, "Export Pattern WAV...", ids.size() == 1);
        menu.addItem(11, "Render Pattern to audio clip", ids.size() == 1);
        menu.addSeparator();
        menu.addItem(9, "Delete empty unused Patterns");
        menu.addItem(10, "Delete Patterns not in Playlist...");
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&list),
                           [safe = juce::Component::SafePointer<ClipPicker>(this), ids,
                            project = studio.controller.projectIdentity,
                            revision = studio.controller.revision](int action)
                           {
                               if (!safe || !action || safe->studio.controller.projectIdentity != project ||
                                   safe->studio.controller.revision != revision)
                                   return;
                               if (action >= 6)
                                   safe->studio.patternBulkAction(action, ids);
                               else if (action == 1)
                                   safe->groupDialog(ids);
                               else if (action == 2)
                                   safe->group(ids, {});
                               else if (ids.size() == 1)
                               {
                                   safe->studio.selectedPattern = ids.front();
                                   if (action == 3)
                                       safe->studio.renamePattern();
                                   else if (action == 4)
                                       safe->studio.transposePattern();
                                   else
                                       safe->studio.showEditor(0);
                                   safe->studio.refresh();
                               }
                           });
    }
    void groupDialog(const std::vector<Id> &ids)
    {
        if (dialog)
        {
            dialog->toFront(true);
            return;
        }
        dialog = std::make_unique<juce::AlertWindow>("Group selected Patterns", "Filter group name",
                                                     juce::MessageBoxIconType::NoIcon);
        dialog->setLookAndFeel(&getLookAndFeel());
        dialog->addTextEditor("name", filterGroup, "Group");
        dialog->addButton("Apply", 1, juce::KeyPress(juce::KeyPress::returnKey));
        dialog->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
        dialog->enterModalState(true,
                                juce::ModalCallbackFunction::create(
                                    [safe = juce::Component::SafePointer<ClipPicker>(this), ids,
                                     project = studio.controller.projectIdentity,
                                     revision = studio.controller.revision](int result)
                                    {
                                        if (!safe || !safe->dialog)
                                            return;
                                        const auto text = safe->dialog->getTextEditorContents("name");
                                        safe->dialog.reset();
                                        if (result && safe->studio.controller.projectIdentity == project &&
                                            safe->studio.controller.revision == revision)
                                            safe->group(ids, text);
                                    }),
                                false);
    }

  public:
    int focusKind() const noexcept { return kind; } // 0 Pattern, 1 audio, 2 automation (clip focus).
    std::function<void(int, Id)> choose;
    static juce::PopupMenu automationSourceMenu(const std::vector<Id> &ids)
    {
        juce::PopupMenu menu;
        menu.addItem(1, "Clone", ids.size() == 1);
        menu.addItem(2, "Clone with links", ids.size() == 1);
        return menu;
    }
    juce::Result cloneAutomationSource(Id source, bool preserveLinks)
    {
        if (kind != 2)
            return juce::Result::fail("automation.wrongPicker: Select the Automation tab");
        CommandArgs args;
        args.target = source;
        args.text = preserveLinks ? "preserve" : "clear";
        const auto revision = studio.controller.revision;
        const auto count = studio.controller.project.automation.sources.size();
        studio.command("automation.source.clone", args);
        if (studio.controller.revision == revision ||
            studio.controller.project.automation.sources.size() != count + 1)
            return juce::Result::fail(studio.status);
        const auto created = studio.controller.project.automation.sources.back().id;
        sync();
        selectIds({created});
        const auto row = std::find(rows.begin(), rows.end(), created);
        if (row != rows.end())
            list.scrollToEnsureRowIsOnscreen(int(row - rows.begin()));
        return juce::Result::ok();
    }
    explicit ClipPicker(Studio &s) : studio(s)
    {
        addAndMakeVisible(list);
        list.setMultipleSelectionEnabled(true);
        list.setRowHeight(54);
        list.setColour(juce::ListBox::backgroundColourId, bg);
        list.setOutlineThickness(0);
        for (int i = 0; i < 3; ++i)
        {
            auto &tab = tabs[size_t(i)];
            tab.setButtonText(i == 0 ? "Pat" : i == 1 ? "Audio" : "Auto");
            tab.setTooltip(i == 0 ? "Patterns" : i == 1 ? "Audio Clip sources" : "Automation sources");
            tab.onClick = [this, i] { setKind(i); };
            addAndMakeVisible(tab);
        }
        search.setFont(juce::Font(vibera::uiFont, 13.f, juce::Font::plain));
        search.setIndents(6, 5);
        search.setTextToShowWhenEmpty("Filter sources...", muted);
        search.onTextChange = [this] { sync(); };
        addAndMakeVisible(search);
        groups.onChange = [this]
        {
            if (!updating)
            {
                filterGroup = groups.getSelectedId() == 1 ? juce::String{} : groups.getText();
                sync();
            }
        };
        addAndMakeVisible(groups);
        sync();
    }
    ~ClipPicker() override
    {
        if (dialog)
            dialog->exitModalState(0);
        list.setModel(nullptr);
    }
    juce::String sourceKind() const
    {
        return kind == 0 ? "pattern" : kind == 1 ? "audio" : "automation";
    }
    const std::vector<Id> &visibleIds() const
    {
        return rows;
    }
    juce::String filterText() const
    {
        return search.getText();
    }
    juce::String groupFilter() const
    {
        return filterGroup;
    }
    int scrollOffset() const
    {
        return list.getViewport()->getViewPositionY();
    }
    int maximumScrollOffset() const
    {
        return std::max(0, int(rows.size()) * list.getRowHeight() - list.getViewport()->getViewHeight());
    }
    void setScrollOffset(int offset)
    {
        list.getViewport()->setViewPosition(0, juce::jlimit(0, maximumScrollOffset(), offset));
    }
    void groupSelected()
    {
        if (kind != 0)
            setKind(0);
        auto ids = selectedIds();
        if (ids.empty())
            ids = {studio.selectedPattern};
        groupDialog(ids);
    }
    void setFilter(const juce::String &query)
    {
        search.setText(query, false);
        sync();
    }
    void setGroupFilter(const juce::String &name)
    {
        filterGroup = name;
        sync();
    }
    void selectIds(const std::vector<Id> &ids)
    {
        updating = true;
        list.deselectAllRows();
        int last = -1;
        for (int row = 0; row < int(rows.size()); ++row)
            if (std::find(ids.begin(), ids.end(), rows[size_t(row)]) != ids.end())
            {
                list.selectRow(row, true, false);
                last = row;
            }
        updating = false;
        selectedRowsChanged(last);
    }
    juce::var dragDescription()
    {
        return getDragSourceDescription(list.getSelectedRows());
    }
    void setKind(int value)
    {
        kind = juce::jlimit(0, 2, value);
        list.deselectAllRows();
        sync();
    }
    std::vector<Id> selectedIds() const
    {
        std::vector<Id> ids;
        const auto selected = list.getSelectedRows();
        for (int i = 0; i < selected.size(); ++i)
            if (juce::isPositiveAndBelow(selected[i], int(rows.size())))
                ids.push_back(rows[size_t(selected[i])]);
        return ids;
    }
    void group(const std::vector<Id> &ids, const juce::String &name)
    {
        CommandArgs args;
        args.ids = ids;
        args.text = name;
        studio.command("pattern.group", args);
    }
    void sync()
    {
        if (updating)
            return;
        const auto keep = selectedIds();
        const bool replaced = identity != studio.controller.projectIdentity;
        identity = studio.controller.projectIdentity;
        updating = true;
        if (replaced)
            filterGroup.clear();
        juce::StringArray names;
        for (const auto &p : studio.controller.project.patterns)
            if (p.group.isNotEmpty())
                names.addIfNotAlreadyThere(p.group);
        names.sort(true);
        groups.clear(juce::dontSendNotification);
        groups.addItem("All groups", 1);
        for (int i = 0; i < names.size(); ++i)
            groups.addItem(names[i], i + 2);
        if (!names.contains(filterGroup))
            filterGroup.clear();
        groups.setSelectedId(filterGroup.isEmpty() ? 1 : names.indexOf(filterGroup) + 2,
                             juce::dontSendNotification);
        groups.setEnabled(kind == 0);
        rows.clear();
        if (kind == 0)
            for (const auto &p : studio.controller.project.patterns)
                if (filterGroup.isEmpty() || p.group == filterGroup)
                    rows.push_back(p.id);
        if (kind == 1)
            for (const auto &c : studio.controller.project.channels)
                if (c.audioClip)
                    rows.push_back(c.id);
        if (kind == 2)
            for (const auto &a : studio.controller.project.automation.sources)
                rows.push_back(a.id);
        const auto query = search.getText().trim();
        std::erase_if(rows, [&](Id id) { return !name(id).containsIgnoreCase(query); });
        list.deselectAllRows();
        list.updateContent();
        for (int i = 0; i < int(rows.size()); ++i)
            if ((!replaced && std::find(keep.begin(), keep.end(), rows[size_t(i)]) != keep.end()) ||
                (kind == 0 && rows[size_t(i)] == studio.selectedPattern && (replaced || keep.size() <= 1)))
                list.selectRow(i, true, false);
        for (int i = 0; i < 3; ++i)
            tabs[size_t(i)].setToggleState(kind == i, juce::dontSendNotification);
        updating = false;
        repaint();
    }
    void resized() override
    {
        auto area = getLocalBounds().reduced(5);
        auto top = area.removeFromTop(25);
        const int width = top.getWidth() / 3;
        for (auto &tab : tabs)
            tab.setBounds(top.removeFromLeft(width).reduced(1, 0));
        area.removeFromTop(5);
        groups.setBounds(area.removeFromTop(25));
        area.removeFromTop(5);
        search.setBounds(area.removeFromTop(24));
        area.removeFromTop(5);
        list.setBounds(area);
    }
    void paint(juce::Graphics &g) override
    {
        g.fillAll(bg);
        g.setColour(grid);
        g.drawVerticalLine(getWidth() - 1, 0, float(getHeight()));
        if (rows.empty())
            label(g, "No matching sources", {10, 104, getWidth() - 20, 24}, muted, 11);
    }
};
} // namespace vibera
