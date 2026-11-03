// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Studio.h"
#include "PreviewPlayerView.h"
#include "BrowserSearchService.h"
#include "BrowserCollections.h"
namespace vibera
{
// Filesystem work belongs to JUCE's directory thread, never to audio processing.
class SampleFileBrowser final : public juce::Component,
                                private juce::FileBrowserListener,
                                private juce::KeyListener,
                                private juce::Timer,
                                public juce::FileDragAndDropTarget
{
    class Filter final : public juce::FileFilter
    {
        mutable juce::CriticalSection mutex;
        juce::String query;

      public:
        Filter() : juce::FileFilter("Samples and MIDI") {}
        void setQuery(juce::String text)
        {
            const juce::ScopedLock guard(mutex);
            query = text;
        }
        bool isFileSuitable(const juce::File &file) const override
        {
            const juce::ScopedLock guard(mutex);
            return file.hasFileExtension("wav;aif;aiff;flac;mid;midi") &&
                   file.getFileName().containsIgnoreCase(query);
        }
        bool isDirectorySuitable(const juce::File &) const override
        {
            return true;
        }
    } filter;
    Studio &studio;
    juce::TimeSliceThread directoryThread{"VibeRa Browser directory scan"};
    juce::DirectoryContentsList files{&filter, directoryThread};
    juce::FileTreeComponent tree{files};
    juce::Component content;
    juce::Viewport viewport;
    juce::TextButton choose{"Folder..."}, up{"Up"}, reread{"Refresh"};
    juce::TextButton back{"< Back"}, forward{"Forward >"};
    ToolbarButton previewStop{"Stop preview"};
    ToolbarButton previewPlay{"Play"};
    PreviewPlayerView playerView;
    std::unique_ptr<browser_search::Service> searchService;
    std::shared_ptr<const browser_search::Service::Result> searchData;
    juce::ToggleButton librarySearch{"All folders"};
    juce::Label searchStatus;
    browser_search::Collections collections;
    browser_search::TagFilter activeFilter;
    bool tagsShown = false;
    juce::TextButton tagsButton{"Tags"}, tagLogic{"ANY"};
    ToolbarButton clearTagsButton{"Clear tag filters"};
    juce::TextEditor tagNameSearch;
    juce::Label tagSummary, tagEmpty;
    juce::StringArray tagUniverse, visibleTags;
    std::shared_ptr<const browser_search::TagCatalog> universeCatalog;
    struct TagModel final : juce::ListBoxModel
    {
        SampleFileBrowser &owner;
        explicit TagModel(SampleFileBrowser &value) : owner(value) {}
        int getNumRows() override
        {
            return owner.visibleTags.size();
        }
        void paintListBoxItem(int row, juce::Graphics &g, int width, int height, bool selected) override
        {
            if (!juce::isPositiveAndBelow(row, owner.visibleTags.size()))
                return;
            const auto &tag = owner.visibleTags[row];
            const bool checked = owner.activeFilter.selected.contains(tag, true);
            if (selected)
                g.fillAll(panel.brighter(.06f));
            g.setColour(checked ? accent : muted);
            g.drawRoundedRectangle(5.f, float(height / 2 - 6), 12.f, 12.f, 2.f, 1.f);
            if (checked)
            {
                juce::Path tick;
                tick.startNewSubPath(7.f, float(height / 2));
                tick.lineTo(10.f, float(height / 2 + 3));
                tick.lineTo(15.f, float(height / 2 - 3));
                g.strokePath(tick, juce::PathStrokeType(1.5f));
            }
            g.setColour(ink);
            g.setFont(juce::Font(vibera::uiFont, 12.f, juce::Font::plain));
            g.drawFittedText(tag, 24, 0, width - 28, height, juce::Justification::centredLeft, 1);
        }
        void listBoxItemClicked(int row, const juce::MouseEvent &event) override
        {
            if (event.mods.isLeftButtonDown())
                owner.toggleFilterTag(row);
        }
        juce::String getTooltipForRow(int row) override
        {
            return juce::isPositiveAndBelow(row, owner.visibleTags.size()) ? owner.visibleTags[row]
                                                                           : juce::String{};
        }
    } tagModel{*this};
    struct TagList final : juce::ListBox
    {
        SampleFileBrowser &owner;
        explicit TagList(SampleFileBrowser &value) : ListBox("Browser tags", &value.tagModel), owner(value) {}
        bool keyPressed(const juce::KeyPress &key) override
        {
            if (key.getKeyCode() == juce::KeyPress::spaceKey || key.getKeyCode() == juce::KeyPress::returnKey)
            {
                owner.toggleFilterTag(getSelectedRow());
                return true;
            }
            return ListBox::keyPressed(key);
        }
    } tagList{*this};
    bool favoritesView() const
    {
        return currentView == 1 ||
               (currentView >= 2 && collections.searches[std::size_t(currentView - 2)].starred);
    }
    void refreshTagChoices()
    {
        if (universeCatalog != collections.tags)
        {
            tagUniverse = collections.tags->allTags();
            universeCatalog = collections.tags;
        }
        auto choices = tagUniverse;
        for (const auto &tag : activeFilter.selected)
            choices.addIfNotAlreadyThere(tag, true);
        choices.sort(true);
        visibleTags.clear();
        int widest = 190;
        for (const auto &tag : choices)
            if (tag.containsIgnoreCase(tagNameSearch.getText().trim()))
            {
                visibleTags.add(tag);
                widest = std::max(widest, tag.length() * 10 + 30);
            }
        tagList.setMinimumContentWidth(widest);
        tagList.updateContent();
        tagList.repaint();
        tagEmpty.setVisible(tagsShown && visibleTags.isEmpty());
    }
    void refreshTagControls()
    {
        tagsButton.setToggleState(tagsShown || !activeFilter.empty(), juce::dontSendNotification);
        tagsButton.setTooltip("Show/hide tags. Right-click a file to add/edit tags. " +
                              juce::String(activeFilter.selected.size()) + " selected (" +
                              (activeFilter.all ? "ALL" : "ANY") + ")");
        tagLogic.setButtonText(activeFilter.all ? "ALL" : "ANY");
        tagLogic.setTooltip("Match " + juce::String(activeFilter.all ? "all" : "any") +
                            " selected tags; click to change");
        tagSummary.setText(activeFilter.selected.joinIntoString(", "), juce::dontSendNotification);
        tagSummary.setTooltip(activeFilter.selected.joinIntoString(", "));
        tagNameSearch.setVisible(tagsShown);
        tagList.setVisible(tagsShown);
        tagLogic.setVisible(tagsShown || !activeFilter.empty());
        clearTagsButton.setVisible(tagsShown || !activeFilter.empty());
        tagSummary.setVisible(!tagsShown && !activeFilter.empty());
        refreshTagChoices();
    }
    juce::Result setTagFilter(browser_search::TagFilter value, bool persist = true)
    {
        const auto valid = browser_search::validateTags(value.selected);
        if (valid.failed())
            return valid;
        activeFilter = std::move(value);
        if (currentView == 0)
            collections.allFilter = activeFilter;
        else if (currentView == 1)
            collections.starredFilter = activeFilter;
        if (!activeFilter.empty())
            librarySearch.setToggleState(true, juce::dontSendNotification);
        updateSearch();
        if (persist && currentView < 2 && !saveLibraries())
            return juce::Result::fail(studio.status);
        return juce::Result::ok();
    }
    void toggleFilterTag(int row)
    {
        if (!juce::isPositiveAndBelow(row, visibleTags.size()))
            return;
        auto changed = activeFilter;
        const auto name = visibleTags[row];
        const int at = changed.selected.indexOf(name, true);
        if (at >= 0)
            changed.selected.remove(at);
        else
            changed.selected.add(name);
        const auto result = setTagFilter(std::move(changed));
        if (result.failed())
            studio.status = result.getErrorMessage();
    }
    juce::Result setFileTags(const juce::File &file, const juce::StringArray &names)
    {
        const auto result = collections.setTags(file, names);
        if (result.failed())
            return result;
        updateSearch();
        if (!saveLibraries())
            return juce::Result::fail(studio.status);
        studio.status = "Tags updated for " + file.getFileName();
        return juce::Result::ok();
    }
    void editFileTags(const juce::File &file, bool append)
    {
        if (file == juce::File{} || !file.hasFileExtension(browser_search::audioExtensions))
        {
            studio.status = "Select a sample or MIDI file to edit its tags";
            return;
        }
        const auto original = collections.tags->tagsFor(file.getFullPathName().toStdString());
        auto *dialog = new CollectionDialog(
            append ? "Add file tags" : "Edit file tags",
            "Comma-separated tags. Editing to an empty list removes tags. Sample contents stay unchanged.");
        dialog->addTextEditor("tags", append ? juce::String{} : original.joinIntoString(", "), "Tags");
        dialog->getTextEditor("tags")->setInputRestrictions(4096);
        dialog->addButton("Apply", 1, juce::KeyPress(juce::KeyPress::returnKey));
        dialog->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
        auto safe = juce::Component::SafePointer<SampleFileBrowser>(this);
        dialog->enterModalState(
            true,
            juce::ModalCallbackFunction::create(
                [safe, dialog, file, append, original](int choice)
                {
                    if (!safe || choice != 1)
                        return;
                    juce::StringArray values;
                    const auto parsed =
                        browser_search::parseTags(dialog->getTextEditorContents("tags"), values);
                    if (parsed.failed())
                    {
                        safe->studio.status = parsed.getErrorMessage();
                        return;
                    }
                    const auto current =
                        safe->collections.tags->tagsFor(file.getFullPathName().toStdString());
                    if (append)
                    {
                        auto merged = current;
                        for (const auto &tag : values)
                            merged.addIfNotAlreadyThere(tag, true);
                        values = std::move(merged);
                    }
                    else if (current != original)
                    {
                        safe->studio.status =
                            "File tags changed while this dialog was open; reopen before replacing";
                        return;
                    }
                    const auto applied = safe->setFileTags(file, values);
                    if (applied.failed())
                        safe->studio.status = applied.getErrorMessage();
                }),
            true);
    }
    struct Views final : juce::TabbedButtonBar
    {
        struct Tab final : juce::TabBarButton
        {
            Tab(const juce::String &name, juce::TabbedButtonBar &bar) : TabBarButton(name, bar) {}
            void paintButton(juce::Graphics &g, bool over, bool down) override
            {
                const auto area = getLocalBounds().toFloat().reduced(1);
                g.setColour(getToggleState() ? panel.interpolatedWith(accent, .16f)
                                             : panel.brighter(over || down ? .08f : 0));
                g.fillRoundedRectangle(area, 3);
                g.setColour(ink);
                g.setFont(juce::Font(vibera::uiFont, 12.f, juce::Font::plain));
                g.drawFittedText(getButtonText(), getLocalBounds().reduced(6, 2),
                                 juce::Justification::centred, 1);
                if (getToggleState())
                    g.fillRect(3, getHeight() - 3, getWidth() - 6, 2);
            }
            int getBestTabLength(int) override
            {
                return juce::jlimit(54, 170,
                                    juce::roundToInt(juce::GlyphArrangement::getStringWidth(
                                        juce::Font(vibera::uiFont, 12.f, juce::Font::plain), getButtonText())) +
                                        20);
            }
        };
        SampleFileBrowser &owner;
        explicit Views(SampleFileBrowser &value)
            : TabbedButtonBar(juce::TabbedButtonBar::TabsAtTop), owner(value)
        {
        }
        juce::TabBarButton *createTabButton(const juce::String &name, int) override
        {
            return new Tab(name, *this);
        }
        void currentTabChanged(int index, const juce::String &) override
        {
            if (!owner.updatingViews)
                owner.selectView(index);
        }
    } views{*this};
    juce::TextButton viewMenu{"..."};
    int currentView = 0;
    bool updatingViews = false, preferencesWritable = true;
    class CollectionDialog final : public juce::AlertWindow
    {
        WorkspaceLook theme;

      public:
        explicit CollectionDialog(
            const juce::String &title,
            const juce::String &message =
                "This changes Browser preferences; sample files and the project stay intact.")
            : AlertWindow(title, message, juce::MessageBoxIconType::NoIcon)
        {
            setLookAndFeel(&theme);
        }
        ~CollectionDialog() override
        {
            setLookAndFeel(nullptr);
        }
    };
    juce::File selectedFile() const
    {
        return searchResults.isVisible() ? resultModel.file(searchResults.getSelectedRow())
                                         : tree.getSelectedFile();
    }
    void rebuildViews(int selected)
    {
        updatingViews = true;
        views.clearTabs();
        views.addTab("All", panel, 0);
        views.addTab("Starred", panel, 1);
        for (const auto &item : collections.searches)
        {
            views.addTab(item.name, panel, views.getNumTabs());
            if (auto *button = views.getTabButton(views.getNumTabs() - 1))
            {
                juce::String scope = item.query;
                for (const auto &root : item.roots)
                    scope += "\n" + juce::String(root);
                if (item.starred)
                    scope += "\nStarred files (current favorites)";
                if (!item.filter.empty())
                    scope += "\n" + juce::String(item.filter.all ? "ALL: " : "ANY: ") +
                             item.filter.selected.joinIntoString(", ");
                button->setTooltip(scope);
            }
        }
        currentView = juce::jlimit(0, views.getNumTabs() - 1, selected);
        views.setCurrentTabIndex(currentView, false);
        updatingViews = false;
    }
    void selectView(int selected)
    {
        currentView = juce::jlimit(0, views.getNumTabs() - 1, selected);
        activeFilter = currentView == 0   ? collections.allFilter
                       : currentView == 1 ? collections.starredFilter
                                          : collections.searches[std::size_t(currentView - 2)].filter;
        tagNameSearch.setText({}, false);
        if (currentView != 0)
            librarySearch.setToggleState(true, juce::dontSendNotification);
        if (currentView >= 2)
            search.setText(collections.searches[std::size_t(currentView - 2)].query, false);
        else
            search.setText({}, false);
        updateSearch();
    }
    void toggleStar(const juce::File &file)
    {
        const auto result = collections.toggle(file);
        if (result.failed())
        {
            studio.status = result.getErrorMessage();
            return;
        }
        if (saveLibraries())
            studio.status =
                collections.starred(file) ? "Added to Starred" : "Removed from Starred (file kept)";
        if (favoritesView())
            updateSearch();
    }
    juce::Result saveSearch(const juce::String &name)
    {
        if (!librarySearch.getToggleState() || !searchData || searchData->error.isNotEmpty())
            return juce::Result::fail("Finish a library search before saving its tab");
        const auto result = collections.save(name, search.getText(),
                                             favoritesView() ? std::vector<std::string>{} : searchRoots(),
                                             activeFilter, favoritesView());
        if (result.failed())
            return result;
        rebuildViews(int(collections.searches.size()) + 1);
        selectView(currentView);
        return saveLibraries() ? juce::Result::ok() : juce::Result::fail(studio.status);
    }
    void collectionMenu()
    {
        juce::PopupMenu menu;
        menu.addItem(1, "Save search result as tab...",
                     searchData && searchData->error.isEmpty() &&
                         (search.getText().trim().isNotEmpty() || !activeFilter.empty()));
        const auto file = selectedFile();
        menu.addItem(2, collections.starred(file) ? "Remove from Starred" : "Add to Starred",
                     file != juce::File{} && file.hasFileExtension(browser_search::audioExtensions));
        menu.addSeparator();
        menu.addItem(3, "Rename search tab...", currentView >= 2);
        menu.addItem(4, "Delete search tab (keep files)", currentView >= 2);
        const auto id =
            currentView >= 2 ? collections.searches[std::size_t(currentView - 2)].id : juce::String{};
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&viewMenu),
                           collectionAction(file, id));
    }
    std::function<void(int)> collectionAction(const juce::File &file, const juce::String &id)
    {
        auto safe = juce::Component::SafePointer<SampleFileBrowser>(this);
        return [safe, file, id](int choice)
        {
            if (!safe || !choice)
                return;
            if (choice == 2)
            {
                safe->toggleStar(file);
                return;
            }
            auto found = std::find_if(safe->collections.searches.begin(), safe->collections.searches.end(),
                                      [&](const auto &item) { return item.id == id; });
            if (choice == 4)
            {
                if (found == safe->collections.searches.end())
                    return;
                safe->collections.searches.erase(found);
                safe->rebuildViews(0);
                safe->selectView(0);
                safe->saveLibraries();
                return;
            }
            if (choice != 1 && (choice != 3 || found == safe->collections.searches.end()))
                return;
            const auto query = safe->search.getText();
            const auto roots = safe->favoritesView() ? std::vector<std::string>{} : safe->searchRoots();
            const auto filter = safe->activeFilter;
            const bool starred = safe->favoritesView();
            auto *dialog = new CollectionDialog(choice == 1 ? "Save Browser search" : "Rename search tab");
            dialog->addTextEditor(
                "name",
                choice == 1
                    ? (query.trim().isEmpty() ? filter.selected.joinIntoString(", ") : query).substring(0, 36)
                    : found->name,
                "Tab name");
            dialog->getTextEditor("name")->setInputRestrictions(48);
            dialog->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
            dialog->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
            dialog->enterModalState(
                true,
                juce::ModalCallbackFunction::create(
                    [safe, dialog, choice, id, query, roots, filter, starred](int result)
                    {
                        if (!safe || result != 1)
                            return;
                        const auto name = dialog->getTextEditorContents("name").trim();
                        if (name.isEmpty())
                        {
                            safe->studio.status = "Search tab name cannot be empty";
                            return;
                        }
                        if (choice == 1)
                        {
                            const auto saved = safe->collections.save(name, query, roots, filter, starred);
                            if (saved.failed())
                            {
                                safe->studio.status = saved.getErrorMessage();
                                return;
                            }
                            safe->rebuildViews(int(safe->collections.searches.size()) + 1);
                            safe->selectView(safe->currentView);
                        }
                        else
                        {
                            auto target = std::find_if(safe->collections.searches.begin(),
                                                       safe->collections.searches.end(),
                                                       [&](const auto &item) { return item.id == id; });
                            if (target == safe->collections.searches.end())
                                return;
                            target->name = name;
                            safe->rebuildViews(safe->currentView);
                        }
                        safe->saveLibraries();
                    }),
                true);
        };
    }
    struct ResultModel final : juce::ListBoxModel
    {
        SampleFileBrowser &owner;
        explicit ResultModel(SampleFileBrowser &o) : owner(o) {}
        int getNumRows() override
        {
            return owner.searchData ? int(owner.searchData->matches.size()) : 0;
        }
        juce::File file(int row) const
        {
            return row >= 0 && row < getCount() ? juce::File(owner.searchData->matches[std::size_t(row)].path)
                                                : juce::File{};
        }
        int getCount() const
        {
            return owner.searchData ? int(owner.searchData->matches.size()) : 0;
        }
        void paintListBoxItem(int row, juce::Graphics &g, int width, int height, bool selected) override
        {
            if (row < 0 || row >= getCount())
                return;
            const auto &entry = owner.searchData->matches[std::size_t(row)];
            if (selected)
                g.fillAll(accent.withAlpha(.22f));
            g.setColour(ink);
            g.setFont(juce::Font(vibera::uiFont, 13.f, juce::Font::plain));
            g.drawFittedText(juce::String(entry.name) + (entry.missing ? " (missing)" : ""), 5, 1, width - 10,
                             18, juce::Justification::centredLeft, 1);
            g.setColour(muted);
            g.setFont(juce::Font(vibera::uiFont, 10.f, juce::Font::plain));
            const auto details =
                juce::File(juce::String(entry.parent)).getFileName() +
                (entry.tags.isEmpty() ? juce::String{} : " | " + entry.tags.joinIntoString(", "));
            g.drawFittedText(details, 5, 19, width - 10, height - 20, juce::Justification::centredLeft, 1);
        }
        juce::String getTooltipForRow(int row) override
        {
            return file(row).getFullPathName() +
                   (juce::isPositiveAndBelow(row, getCount()) &&
                            !owner.searchData->matches[std::size_t(row)].tags.isEmpty()
                        ? "\nTags: " + owner.searchData->matches[std::size_t(row)].tags.joinIntoString(", ")
                        : juce::String{});
        }
        void selectedRowsChanged(int row) override
        {
            owner.auditionSelection(file(row));
        }
        void listBoxItemClicked(int row, const juce::MouseEvent &event) override
        {
            owner.fileClicked(file(row), event);
        }
        void listBoxItemDoubleClicked(int row, const juce::MouseEvent &) override
        {
            owner.load(file(row), false);
        }
        juce::var getDragSourceDescription(const juce::SparseSet<int> &rows) override
        {
            return rows.size() == 1 ? juce::var(file(rows[0]).getFullPathName()) : juce::var{};
        }
    } resultModel{*this};
    juce::ListBox searchResults{"Library search", &resultModel};
    std::vector<std::string> searchRoots() const
    {
        if (currentView >= 2 && !collections.searches[std::size_t(currentView - 2)].starred)
            return collections.searches[std::size_t(currentView - 2)].roots;
        std::vector<std::string> paths;
        for (const auto &root : roots)
            paths.push_back(root.toStdString());
        if (paths.empty() && hasRoot())
            paths.push_back(files.getDirectory().getFullPathName().toStdString());
        return paths;
    }
    void updateSearch(bool refresh = false)
    {
        if (searchService->terminalFlag()->load())
        {
            searchData.reset();
            searchService = std::make_unique<browser_search::Service>(
                preferences == juce::File{} ? juce::File{}
                                            : preferences.getSiblingFile("browser-search-index.json"));
        }
        if (!activeFilter.empty())
            librarySearch.setToggleState(true, juce::dontSendNotification);
        const bool global = librarySearch.getToggleState();
        librarySearch.setEnabled(currentView == 0 && activeFilter.empty());
        librarySearch.setVisible(currentView == 0);
        search.setTextToShowWhenEmpty(favoritesView()    ? "Search Starred..."
                                      : currentView >= 2 ? "Search this tab..."
                                      : global           ? "Search saved folders..."
                                                         : "Filter files in this tree...",
                                      muted);
        const bool showing =
            global && (favoritesView() || search.getText().trim().isNotEmpty() || !activeFilter.empty());
        tree.setVisible(!showing);
        searchResults.setVisible(showing);
        searchStatus.setVisible(global);
        refreshTagControls();
        if (!global)
        {
            searchService->cancel();
            searchData.reset();
            searchResults.updateContent();
            filter.setQuery(search.getText());
            files.refresh();
        }
        else if (!showing)
        {
            searchService->cancel();
            searchData.reset();
            searchResults.updateContent();
            filter.setQuery({});
            files.refresh();
            searchStatus.setText("Search saved folders and subfolders", juce::dontSendNotification);
        }
        else
        {
            searchData.reset();
            searchResults.updateContent();
            auto query = browser_search::parseQuery(search.getText());
            query.favoritesOnly = favoritesView();
            query.tagCatalog = collections.tags;
            query.tagFilter = activeFilter;
            if (query.favoritesOnly)
                for (const auto &file : collections.favorites)
                    query.favoritePaths.push_back(file.toStdString());
            searchService->request(searchRoots(), std::move(query), refresh);
            searchStatus.setText("Searching... Esc cancels", juce::dontSendNotification);
        }
        resized();
    }
    void timerCallback() override
    {
        previewPlay.setEnabled(studio.audio.canReplayAudition());
        playerView.repaint();
        if (auto result = searchService->takeResult())
        {
            searchData = std::move(result);
            searchResults.setMinimumContentWidth(
                std::max(190, int(std::min<std::size_t>(1600, searchData->widestRow * 7 + 16))));
            searchResults.deselectAllRows();
            searchResults.updateContent();
            searchStatus.setText(
                searchData->error.isNotEmpty()
                    ? searchData->error
                    : juce::String(int(searchData->matches.size())) + " matches / " +
                          juce::String(int(searchData->stats.files)) + " indexed" +
                          (searchData->warning.isNotEmpty() ? " | " + searchData->warning : ""),
                juce::dontSendNotification);
        }
        else if (librarySearch.getToggleState() &&
                 searchService->phase() == browser_search::Service::Phase::indexing)
            searchStatus.setText("Indexing " + juce::String(juce::int64(searchService->indexedFiles())) +
                                     " files... Esc cancels",
                                 juce::dontSendNotification);
        else if (librarySearch.getToggleState() && searchService->terminalFlag()->load())
            searchStatus.setText("Search unavailable; Refresh to retry", juce::dontSendNotification);
    }
    juce::ToggleButton fullPreview{"Full sample"};
    juce::Slider previewVolume;
    juce::Label volumeLabel;
    juce::StringArray history;
    int historyIndex = -1;
    void updateNavigation()
    {
        back.setEnabled(historyIndex > 0);
        forward.setEnabled(historyIndex >= 0 && historyIndex + 1 < history.size());
        up.setEnabled(hasRoot() && files.getDirectory().getParentDirectory() != files.getDirectory());
    }
    void navigateHistory(int delta)
    {
        const int target = historyIndex + delta;
        if (!juce::isPositiveAndBelow(target, history.size()))
            return;
        const juce::File root(history[target]);
        if (!root.isDirectory())
        {
            studio.status = "Browser history folder is missing; reconnect it or choose another folder";
            return;
        }
        historyIndex = target;
        setRoot(root, false, true, false);
    }
    juce::TextEditor search;
    juce::ComboBox libraries;
    juce::TextButton forget{"-"};
    juce::StringArray roots;
    juce::File preferences;
    bool saveLibraries()
    {
        if (preferences == juce::File{})
            return true;
        if (!preferencesWritable)
        {
            studio.status = "Browser preferences are invalid/unsupported; original file preserved";
            return false;
        }
        auto state = collections.state();
        auto *data = state.getDynamicObject();
        juce::Array<juce::var> entries;
        for (const auto &root : roots)
            entries.add(root);
        data->setProperty("roots", entries);
        data->setProperty("active", files.getDirectory().getFullPathName());
        const auto encoded = juce::JSON::toString(state);
        if (encoded.getNumBytesAsUTF8() > 2097152)
        {
            studio.status = "Browser preferences exceed 2 MiB; previous file preserved";
            return false;
        }
        juce::TemporaryFile temporary(preferences);
        if (!preferences.getParentDirectory().createDirectory() ||
            !temporary.getFile().replaceWithText(encoded) || !temporary.overwriteTargetFileWithTemporary())
        {
            studio.status = "Could not save Browser folders";
            return false;
        }
        return true;
    }
    void updateLibraries()
    {
        libraries.clear(juce::dontSendNotification);
        for (int i = 0; i < roots.size(); ++i)
        {
            const juce::File root(roots[i]);
            libraries.addItem((root.getFileName().isEmpty() ? root.getFullPathName() : root.getFileName()) +
                                  (root.isDirectory() ? "" : " (missing)") + " | " + root.getFullPathName(),
                              i + 1);
        }
        libraries.setSelectedId(roots.indexOf(files.getDirectory().getFullPathName()) + 1,
                                juce::dontSendNotification);
        if (libraries.getSelectedId() == 0 && hasRoot())
            libraries.setText(files.getDirectory().getFileName(), juce::dontSendNotification);
        libraries.setTooltip(hasRoot() ? files.getDirectory().getFullPathName()
                                       : "Saved sample/MIDI folders");
        forget.setEnabled(libraries.getSelectedId() > 0);
    }
    std::unique_ptr<juce::FileChooser> chooser;
    void selectionChanged() override
    {
        studio.focus = Scope::browser;
        const auto file = tree.getSelectedFile();
        tree.setDragAndDropDescription(file.existsAsFile() ? file.getFullPathName() : juce::String{});
        auditionSelection(file);
    }
    void auditionSelection(const juce::File &file)
    {
        studio.focus = Scope::browser;
        if (!file.existsAsFile() || file.hasFileExtension("mid;midi"))
            return;
        studio.audio.requestAudition(file.getFullPathName());
    }
    void load(const juce::File &file, bool replace)
    {
        if (!file.existsAsFile())
        {
            studio.status = "Browser file is missing; refresh the search or reconnect the folder";
            return;
        }
        if (file.hasFileExtension("mid;midi"))
            studio.importMidiFile(file);
        else
            studio.loadSample(file.getFullPathName(), replace, replace ? studio.selectedChannel : 0);
    }
    void fileClicked(const juce::File &file, const juce::MouseEvent &event) override
    {
        studio.focus = Scope::browser;
        if (file.isDirectory() && !event.mods.isPopupMenu())
        {
            if (auto *item = tree.getSelectedItem(0))
                item->setOpen(!item->isOpen());
            return;
        }
        if (event.mods.isMiddleButtonDown())
        {
            load(file, true);
            return;
        }
        if (event.mods.isLeftButtonDown() && file.existsAsFile() &&
            file.hasFileExtension("wav;aif;aiff;flac"))
        {
            // Also audition an already-selected item; a selection notification
            // and this click coalesce before the control-thread handoff.
            studio.audio.requestAudition(file.getFullPathName(), event.mods.isAltDown());
            return;
        }
        if (!event.mods.isPopupMenu() || file == juce::File{})
            return;
        juce::PopupMenu menu;
        menu.addItem(1,
                     file.hasFileExtension("mid;midi") ? "Import into selected Channel"
                                                       : "Send to selected Channel",
                     file.existsAsFile());
        if (!file.hasFileExtension("mid;midi"))
            menu.addItem(2, "Open in new sample Channel", file.existsAsFile());
        menu.addItem(3, collections.starred(file) ? "Remove from Starred" : "Add to Starred",
                     file.hasFileExtension(browser_search::audioExtensions));
        const bool taggable = !file.isDirectory() && file.hasFileExtension(browser_search::audioExtensions);
        menu.addSeparator();
        menu.addItem(4, "Add tags...", taggable);
        menu.addItem(5, "Edit tags...", taggable);
        auto safe = juce::Component::SafePointer<SampleFileBrowser>(this);
        menu.showMenuAsync(
            juce::PopupMenu::Options().withTargetComponent(
                searchResults.isVisible() ? static_cast<juce::Component *>(&searchResults) : &tree),
            [safe, file](int choice)
            {
                if (safe && (choice == 4 || choice == 5))
                    safe->editFileTags(file, choice == 4);
                else if (safe && choice == 3)
                    safe->toggleStar(file);
                else if (safe && choice)
                    safe->load(file, choice == 1);
            });
    }
    void fileDoubleClicked(const juce::File &file) override
    {
        load(file, false);
    }
    void browserRootChanged(const juce::File &) override {}

  public:
    std::function<void()> rootChanged;
    bool hasRoot() const
    {
        return files.getDirectory() != juce::File{};
    }
    explicit SampleFileBrowser(Studio &owner, juce::File prefs = {})
        : studio(owner), playerView(owner), preferences(prefs)
    {
        searchService = std::make_unique<browser_search::Service>(
            preferences == juce::File{} ? juce::File{}
                                        : preferences.getSiblingFile("browser-search-index.json"));
        setOpaque(true);
        addAndMakeVisible(viewport);
        viewport.setViewedComponent(&content, false);
        viewport.setScrollBarsShown(true, true);
        for (auto *component : std::initializer_list<juce::Component *>{
                 &choose,        &up,          &reread,      &back,          &forward,         &search,
                 &libraries,     &forget,      &previewStop, &previewPlay,   &playerView,      &fullPreview,
                 &previewVolume, &volumeLabel, &tree,        &librarySearch, &searchStatus,    &searchResults,
                 &views,         &viewMenu,    &tagsButton,  &tagLogic,      &clearTagsButton, &tagNameSearch,
                 &tagSummary,    &tagEmpty,    &tagList})
        {
            content.addAndMakeVisible(component);
            component->addKeyListener(this);
        }
        // Audition transport stays reachable while folders/results/waveform scroll.
        for (auto *component : std::initializer_list<juce::Component *>{
                 &previewPlay, &previewStop, &fullPreview, &previewVolume, &volumeLabel})
            addAndMakeVisible(component);
        search.setFont(juce::Font(vibera::uiFont, 13.f, juce::Font::plain));
        search.setIndents(6, 5);
        search.setTextToShowWhenEmpty("Filter files in this tree...", muted);
        librarySearch.setTooltip("Search only your saved library folders and their subfolders (Ctrl+F)");
        librarySearch.onClick = [this]
        {
            search.setTextToShowWhenEmpty(librarySearch.getToggleState() ? "Search saved folders..."
                                                                         : "Filter files in this tree...",
                                          muted);
            updateSearch();
        };
        searchResults.setRowHeight(36);
        searchResults.setColour(juce::ListBox::backgroundColourId, bg);
        searchResults.setVisible(false);
        searchStatus.setVisible(false);
        searchStatus.setFont(juce::Font(vibera::uiFont, 10.f, juce::Font::plain));
        searchStatus.setTooltip("Search: k?ck*, kick OR snare, (kick OR snare) AND NOT wet. "
                                "Uppercase NOT > AND > OR; quotes keep phrases literal. "
                                "Plain text keeps phrase matching. type:wav;flac and folder:\"path\" "
                                "filter the whole expression. Refresh rereads your library.");
        search.setTooltip(searchStatus.getTooltip());
        viewMenu.setTooltip("Save/rename/delete search tabs and Starred file references");
        viewMenu.onClick = [this] { collectionMenu(); };
        views.setMinimumTabScaleFactor(1.0);
        views.addMouseListener(this, true);
        rebuildViews(0);
        tagsButton.onClick = [this] { viewAction("browser.showTags"); };
        tagLogic.onClick = [this] { viewAction("browser.matchAllTags"); };
        clearTagsButton.setIcon(ToolbarButton::Icon::close);
        clearTagsButton.setTooltip("Clear selected tag filters; keep file tag assignments");
        clearTagsButton.onClick = [this] { viewAction("browser.clearTagFilters"); };
        tagNameSearch.setTextToShowWhenEmpty("Filter tags...", muted);
        tagNameSearch.setInputRestrictions(64);
        tagNameSearch.onTextChange = [this] { refreshTagChoices(); };
        tagList.setRowHeight(24);
        tagList.setColour(juce::ListBox::backgroundColourId, bg);
        tagList.getViewport()->setScrollBarsShown(true, true);
        tagSummary.setFont(juce::Font(vibera::uiFont, 11.f, juce::Font::plain));
        tagEmpty.setText("No tags. Right-click a file to add tags.", juce::dontSendNotification);
        tagEmpty.setFont(juce::Font(vibera::uiFont, 11.f, juce::Font::plain));
        tagEmpty.setInterceptsMouseClicks(false, false);
        startTimerHz(15);
        previewPlay.setIcon(ToolbarButton::Icon::play);
        previewStop.setIcon(ToolbarButton::Icon::stop);
        previewPlay.setEnabled(false);
        previewPlay.setTooltip("Replay the selected sample to the end");
        previewPlay.onClick = [this] { viewAction("browser.playPreview"); };
        search.setInputRestrictions(512);
        previewStop.setTooltip("Stop sample audition; keep song transport running");
        previewStop.onClick = [this] { viewAction("browser.stopPreview"); };
        fullPreview.setToggleState(studio.audio.fullAudition(), juce::dontSendNotification);
        fullPreview.setTooltip("Play the entire supported sample instead of a five-second preview");
        fullPreview.onClick = [this] { viewAction("browser.fullPreview"); };
        previewVolume.setRange(0, 100, 1);
        previewVolume.setSliderStyle(juce::Slider::LinearHorizontal);
        previewVolume.setTextBoxStyle(juce::Slider::TextBoxRight, false, 42, 22);
        previewVolume.setTextValueSuffix("%");
        previewVolume.setValue(studio.audio.auditionVolume() * 100, juce::dontSendNotification);
        previewVolume.onValueChange = [this]
        { studio.audio.setAuditionVolume(float(previewVolume.getValue() / 100)); };
        previewVolume.setTooltip("Sample preview volume only; project Mixer is unchanged");
        volumeLabel.setText("Volume", juce::dontSendNotification);
        volumeLabel.setFont(juce::Font(vibera::uiFont, 11.f, juce::Font::plain));
        back.setTooltip("Previous folder (Backspace or Alt+Left while Browser has focus)");
        forward.setTooltip("Next folder (Shift+Backspace or Alt+Right while Browser has focus)");
        back.onClick = [this] { viewAction("browser.back"); };
        forward.onClick = [this] { viewAction("browser.forward"); };

        search.onTextChange = [this] { updateSearch(); };
        up.onClick = [this]
        {
            if (files.getDirectory() != juce::File{})
                setRoot(files.getDirectory().getParentDirectory(), false);
        };
        reread.onClick = [this]
        {
            files.refresh();
            tree.refresh();
            updateSearch(true);
        };
        choose.onClick = [this]
        {
            chooser = std::make_unique<juce::FileChooser>("Choose sample/MIDI folder", files.getDirectory());
            auto safe = juce::Component::SafePointer<SampleFileBrowser>(this);
            chooser->launchAsync(juce::FileBrowserComponent::openMode |
                                     juce::FileBrowserComponent::canSelectDirectories,
                                 [safe](const juce::FileChooser &selection)
                                 {
                                     if (safe && selection.getResult().isDirectory())
                                         safe->setRoot(selection.getResult());
                                 });
        };
        tree.setRootItemVisible(false);
        tree.setMultiSelectEnabled(false);
        tree.setItemHeight(25);
        tree.setColour(juce::TreeView::backgroundColourId, bg);
        tree.setColour(juce::TreeView::linesColourId, grid);
        tree.addListener(this);
        libraries.setTextWhenNothingSelected("Choose or drop a folder");
        libraries.onChange = [this]
        {
            const int index = libraries.getSelectedId() - 1;
            if (juce::isPositiveAndBelow(index, roots.size()))
            {
                const juce::File root(roots[index]);
                if (root.isDirectory())
                    setRoot(root, false);
                else
                    studio.status = "Browser folder is missing; reconnect it or remove its saved entry";
            }
            forget.setEnabled(libraries.getSelectedId() > 0);
        };
        forget.setTooltip("Remove selected saved folder (does not delete files)");
        forget.onClick = [this]
        {
            const int index = libraries.getSelectedId() - 1;
            if (juce::isPositiveAndBelow(index, roots.size()))
            {
                roots.remove(index);
                updateLibraries();
                saveLibraries();
                updateSearch(true);
            }
        };
        if (preferences.existsAsFile())
        {
            const auto data = preferences.getSize() <= 2097152 ? juce::JSON::parse(preferences) : juce::var{};
            const auto restored = collections.restore(data);
            const auto *storedRoots = data["roots"].getArray();
            preferencesWritable = restored.wasOk() && storedRoots && storedRoots->size() <= 32;
            if (preferencesWritable)
            {
                juce::StringArray validatedRoots;
                for (const auto &entry : *storedRoots)
                {
                    if (!entry.isString() || !browser_search::Collections::path(entry.toString()))
                    {
                        preferencesWritable = false;
                        break;
                    }
                    validatedRoots.addIfNotAlreadyThere(entry.toString(), true);
                }
                if (preferencesWritable)
                {
                    roots = std::move(validatedRoots);
                    const auto active = data["active"].toString();
                    if (juce::File::isAbsolutePath(active) && juce::File(active).isDirectory())
                        setRoot(juce::File(active), false, false);
                }
            }
            if (!preferencesWritable)
                studio.status = "Browser preferences invalid/unsupported; original file preserved";
            rebuildViews(0);
        }
        updateLibraries();
        updateNavigation();
        selectView(0);
        directoryThread.startThread();
    }
    ~SampleFileBrowser() override
    {
        stopTimer();
        tagList.setModel(nullptr);
        views.removeMouseListener(this);
        searchResults.setModel(nullptr);
        searchData.reset();
        searchService.reset();
        viewport.setViewedComponent(nullptr, false);
        tree.removeListener(this);
        files.clear();
        directoryThread.stopThread(5000);
    }
    void setRoot(const juce::File &root, bool remember = true, bool persist = true, bool recordHistory = true)
    {
        if (!root.isDirectory())
            return;
        if (currentView != 0)
        {
            rebuildViews(0);
            activeFilter = collections.allFilter;
            librarySearch.setEnabled(true);
            search.setText({}, false);
        }
        studio.focus = Scope::browser;
        if (recordHistory && (historyIndex < 0 || juce::File(history[historyIndex]) != root))
        {
            while (history.size() > historyIndex + 1)
                history.remove(history.size() - 1);
            history.add(root.getFullPathName());
            if (history.size() > 64)
                history.remove(0);
            historyIndex = history.size() - 1;
        }
        files.setDirectory(root, true, true);
        tree.refresh();
        if (remember && !roots.contains(root.getFullPathName(), true))
        {
            if (roots.size() < 32)
                roots.add(root.getFullPathName());
            else
                studio.status = "Browser saved-folder limit reached (32); remove a saved entry first";
        }
        updateLibraries();
        updateNavigation();
        updateSearch();
        if (persist)
            saveLibraries();
        if (rootChanged)
            rootChanged();
    }
    void paint(juce::Graphics &g) override
    {
        g.fillAll(bg);
    }
    void resized() override
    {
        auto body = getLocalBounds();
        auto audition = body.removeFromBottom(std::min(54, getHeight())).reduced(2, 0);
        viewport.setBounds(body);
        const int tagHeight = tagsShown ? 149 : !activeFilter.empty() ? 27 : 0;
        const int minimumHeight = (librarySearch.getToggleState() ? 312 : 276) + tagHeight;
        const bool overflow = body.getHeight() < minimumHeight;
        content.setSize(
            std::max(190, getWidth() - (overflow ? viewport.getScrollBarThickness() : 0)),
            std::max(minimumHeight,
                     body.getHeight() - (getWidth() < 190 ? viewport.getScrollBarThickness() : 0)));
        auto area = content.getLocalBounds().reduced(2);
        auto tabs = area.removeFromTop(27);
        viewMenu.setBounds(tabs.removeFromRight(28));
        tagsButton.setBounds(tabs.removeFromRight(42));
        views.setBounds(tabs);
        area.removeFromTop(3);
        auto buttons = area.removeFromTop(26);
        choose.setBounds(buttons.removeFromLeft(72));
        up.setBounds(buttons.removeFromLeft(35));
        reread.setBounds(buttons);
        area.removeFromTop(3);
        auto nav = area.removeFromTop(24);
        back.setBounds(nav.removeFromLeft(nav.getWidth() / 2));
        forward.setBounds(nav);
        area.removeFromTop(3);
        auto rootRow = area.removeFromTop(24);
        forget.setBounds(rootRow.removeFromRight(24));
        libraries.setBounds(rootRow);
        auto queryRow = area.removeFromTop(24);
        if (librarySearch.isVisible())
            librarySearch.setBounds(queryRow.removeFromRight(94));
        search.setBounds(queryRow);
        if (librarySearch.getToggleState())
            searchStatus.setBounds(area.removeFromTop(20));
        if (tagsShown || !activeFilter.empty())
        {
            auto row = area.removeFromTop(24);
            tagLogic.setBounds(row.removeFromLeft(48));
            clearTagsButton.setBounds(row.removeFromRight(26));
            if (tagsShown)
                tagNameSearch.setBounds(row);
            else
                tagSummary.setBounds(row);
            area.removeFromTop(3);
            if (tagsShown)
            {
                const auto list = area.removeFromTop(120);
                tagList.setBounds(list);
                tagEmpty.setBounds(list.reduced(5));
                area.removeFromTop(2);
            }
        }
        area.removeFromTop(3);
        // Waveform remains scrollable; the pinned transport cannot be clipped by
        // a compact viewport or the parent Browser's project-information footer.
        const auto list = area.removeFromTop(std::max(72, area.getHeight() - 44));
        tree.setBounds(list);
        searchResults.setBounds(list);
        area.removeFromTop(4);
        playerView.setBounds(area.removeFromTop(40));
        audition.removeFromTop(4);
        auto player = audition.removeFromTop(24);
        const auto fullBounds = player.removeFromRight(std::max(70, player.getWidth() - 84));
        fullPreview.setButtonText(fullBounds.getWidth() < 98 ? "Full" : "Full sample");
        fullPreview.setBounds(fullBounds);
        previewPlay.setBounds(player.removeFromLeft(40));
        previewStop.setButtonText("Stop");
        previewStop.setBounds(player);
        auto gain = audition.removeFromTop(26);
        volumeLabel.setBounds(gain.removeFromLeft(44));
        previewVolume.setBounds(gain);
    }
    bool isInterestedInFileDrag(const juce::StringArray &names) override
    {
        return names.size() == 1 && juce::File(names[0]).isDirectory();
    }
    void filesDropped(const juce::StringArray &names, int, int) override
    {
        if (isInterestedInFileDrag(names))
            setRoot(juce::File(names[0]));
    }
    void mouseDown(const juce::MouseEvent &event) override
    {
        if (!event.mods.isPopupMenu())
            return;
        auto *tab = dynamic_cast<juce::TabBarButton *>(event.originalComponent);
        if (!tab && event.originalComponent)
            tab = event.originalComponent->findParentComponentOfClass<juce::TabBarButton>();
        if (tab)
            views.setCurrentTabIndex(tab->getIndex());
        collectionMenu();
    }
    bool viewAction(const juce::String &action)
    {
        if (action == "browser.showTags")
        {
            tagsShown = !tagsShown;
            refreshTagControls();
            resized();
        }
        else if (action == "browser.editTags" || action == "browser.addTags")
            editFileTags(selectedFile(), action == "browser.addTags");
        else if (action == "browser.matchAllTags")
        {
            auto changed = activeFilter;
            changed.all = !changed.all;
            const auto result = setTagFilter(std::move(changed));
            if (result.failed())
                studio.status = result.getErrorMessage();
        }
        else if (action == "browser.clearTagFilters")
        {
            auto changed = activeFilter;
            changed.selected.clear();
            tagNameSearch.setText({}, false);
            const auto result = setTagFilter(std::move(changed));
            if (result.failed())
                studio.status = result.getErrorMessage();
        }
        else if (action == "browser.showAll" || action == "browser.showStarred")
            views.setCurrentTabIndex(action == "browser.showStarred" ? 1 : 0);
        else if (action == "browser.toggleFavorite")
        {
            const auto file = selectedFile();
            if (file != juce::File{})
                toggleStar(file);
        }
        else if (action == "browser.collectionsMenu")
            collectionMenu();
        else if (action == "browser.find")
        {
            studio.focus = Scope::browser;
            librarySearch.setToggleState(true, juce::dontSendNotification);
            librarySearch.onClick();
            search.grabKeyboardFocus();
            search.selectAll();
        }
        else if (action == "browser.nextMatch" || action == "browser.previousMatch")
        {
            const auto count = resultModel.getNumRows();
            if (count > 0)
            {
                const int current = searchResults.getSelectedRow();
                const int next = action == "browser.nextMatch" ? (current + 1) % count
                                                               : (current <= 0 ? count - 1 : current - 1);
                searchResults.selectRow(next);
                searchResults.scrollToEnsureRowIsOnscreen(next);
            }
        }
        else if (action == "browser.reindex")
            reread.onClick();
        else if (action == "browser.back")
            navigateHistory(-1);
        else if (action == "browser.forward")
            navigateHistory(1);
        else if (action == "browser.up")
        {
            if (hasRoot())
                setRoot(files.getDirectory().getParentDirectory(), false);
        }
        else if (action == "browser.clearSearch")
        {
            search.setText({}, false);
            tagNameSearch.setText({}, false);
            tagsShown = false;
            auto changed = activeFilter;
            changed.selected.clear();
            const auto result = setTagFilter(std::move(changed));
            if (result.failed())
                studio.status = result.getErrorMessage();
        }
        else if (action == "browser.fullPreview")
        {
            studio.audio.setFullAudition(!studio.audio.fullAudition());
            fullPreview.setToggleState(studio.audio.fullAudition(), juce::dontSendNotification);
        }
        else if (action == "browser.previewLouder" || action == "browser.previewSofter")
        {
            studio.audio.setAuditionVolume(studio.audio.auditionVolume() +
                                           (action == "browser.previewLouder" ? .05f : -.05f));
            previewVolume.setValue(studio.audio.auditionVolume() * 100, juce::dontSendNotification);
        }
        else if (action == "browser.playPreview")
        {
            const auto result = studio.audio.seekAudition(0);
            if (result.failed())
                studio.status = result.getErrorMessage();
        }
        else if (action == "browser.stopPreview")
            studio.audio.stopAudition();
        else
            return false;
        return true;
    }
    bool keyPressed(const juce::KeyPress &key) override
    {
        const auto *focused = juce::Component::getCurrentlyFocusedComponent();
        if (key.getKeyCode() == juce::KeyPress::backspaceKey &&
            dynamic_cast<const juce::TextEditor *>(focused))
            return false;
        const auto action = studio.customization.resolve(key, Scope::browser);
        return viewAction(action == "selection.cancel" ? "browser.clearSearch" : action);
    }
    bool keyPressed(const juce::KeyPress &key, juce::Component *source) override
    {
        if (key.getKeyCode() == juce::KeyPress::backspaceKey && source &&
            (dynamic_cast<juce::TextEditor *>(source) ||
             source->findParentComponentOfClass<juce::TextEditor>()))
            return false;
        return keyPressed(key);
    }
    juce::Viewport &viewportForTest()
    {
        return viewport;
    }
    PreviewPlayerView &playerForTest()
    {
        return playerView;
    }
    juce::TextButton &playForTest()
    {
        return previewPlay;
    }
    juce::ToggleButton &fullForTest()
    {
        return fullPreview;
    }
    juce::Slider &volumeForTest()
    {
        return previewVolume;
    }
    juce::TextButton &backForTest()
    {
        return back;
    }
    juce::TextButton &forwardForTest()
    {
        return forward;
    }
    juce::TextEditor &searchForTest()
    {
        return search;
    }
    juce::ComboBox &librariesForTest()
    {
        return libraries;
    }
    juce::TextButton &forgetForTest()
    {
        return forget;
    }
    juce::DirectoryContentsList &listingForTest()
    {
        return files;
    }
    juce::FileTreeComponent &treeForTest()
    {
        return tree;
    }
    void filterForTest(const juce::String &query)
    {
        // Submit through the production handler now. A queued TextEditor
        // message otherwise lets a test accept the previous query's rows,
        // then dereference cleared results while waiting for audition I/O.
        search.setText(query, false);
        search.onTextChange();
    }
    void clickForTest(const juce::File &file, const juce::MouseEvent &event)
    {
        fileClicked(file, event);
    }
    void loadForTest(const juce::File &file, bool replace)
    {
        load(file, replace);
    }
    juce::ListBox &resultsForTest()
    {
        return searchResults;
    }
    juce::TabbedButtonBar &viewsForTest()
    {
        return views;
    }
    const browser_search::Collections &collectionsForTest() const
    {
        return collections;
    }
    void starForTest(const juce::File &file)
    {
        toggleStar(file);
    }
    juce::Result saveSearchForTest(const juce::String &name)
    {
        return saveSearch(name);
    }
    void collectionChoiceForTest(int choice)
    {
        const auto id =
            currentView >= 2 ? collections.searches[std::size_t(currentView - 2)].id : juce::String{};
        collectionAction(selectedFile(), id)(choice);
    }
    juce::Result setTagsForTest(const juce::File &file, const juce::StringArray &tags)
    {
        return setFileTags(file, tags);
    }
    juce::Result useOwnedPreferencesForTest(const juce::File &file)
    {
        if (file.exists())
            return juce::Result::fail("Owned test preferences must be a fresh file");
        preferences = file;
        preferencesWritable = true;
        return saveLibraries() ? juce::Result::ok() : juce::Result::fail(studio.status);
    }
    juce::Result filterTagsForTest(browser_search::TagFilter filter)
    {
        return setTagFilter(std::move(filter));
    }
    const browser_search::TagFilter &tagFilterForTest() const
    {
        return activeFilter;
    }
    juce::ListBox &tagListForTest()
    {
        return tagList;
    }
    juce::TextEditor &tagNameSearchForTest()
    {
        return tagNameSearch;
    }
    juce::TextButton &tagLogicForTest()
    {
        return tagLogic;
    }
    void editTagsForTest(const juce::File &file, bool append)
    {
        editFileTags(file, append);
    }
    const browser_search::Service::Result *searchDataForTest() const
    {
        return searchData.get();
    }
    std::shared_ptr<std::atomic<bool>> searchTerminalForTest() const
    {
        return searchService->terminalFlag();
    }
};
} // namespace vibera
