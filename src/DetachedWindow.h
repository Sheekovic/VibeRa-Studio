// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "WindowChrome.h"

namespace vibera
{
// Logical desktop coordinates support monitors to the left/above the primary display.
inline juce::Rectangle<int> recoverDesktopWindow(juce::Rectangle<int> bounds,
                                                 const std::vector<juce::Rectangle<int>> &areas)
{
    if (areas.empty())
        return bounds;
    for (auto area : areas)
    {
        auto visible = area.getIntersection(bounds.withHeight(std::min(36, bounds.getHeight())));
        if (visible.getWidth() >= std::min(120, bounds.getWidth()) && visible.getHeight() >= 30)
            return bounds;
    }
    auto nearest = areas.front();
    auto distance = [&](juce::Rectangle<int> area)
    {
        const auto centre = bounds.getCentre();
        return centre.toDouble().getDistanceSquaredFrom(
            juce::Point<double>(juce::jlimit(area.getX(), area.getRight(), centre.x),
                                juce::jlimit(area.getY(), area.getBottom(), centre.y)));
    };
    for (auto area : areas)
        if (distance(area) < distance(nearest))
            nearest = area;
    bounds.setSize(std::min(bounds.getWidth(), nearest.getWidth()),
                   std::min(bounds.getHeight(), nearest.getHeight()));
    return bounds.withPosition(
        juce::jlimit(nearest.getX(), nearest.getRight() - bounds.getWidth(), bounds.getX()),
        juce::jlimit(nearest.getY(), nearest.getBottom() - bounds.getHeight(), bounds.getY()));
}
inline std::vector<juce::Rectangle<int>> desktopWorkAreas()
{
    std::vector<juce::Rectangle<int>> areas;
    for (const auto &display : juce::Desktop::getInstance().getDisplays().displays)
        areas.push_back(display.userArea);
    return areas;
}

class Studio::DetachedWindow final : public juce::DocumentWindow,
                                     public juce::DragAndDropContainer,
                                     private juce::Timer,
                                     private juce::ComponentPeer::ScaleFactorListener
{
    Studio &owner;
    int index;
    WindowChrome chrome{36};
    ToolbarButton dock{"Dock"};
    juce::Rectangle<int> normalBounds;
    void nativeScaleFactorChanged(double) override
    {
        // WM_DPICHANGED can re-enter native positioning. Reconcile JUCE's logical
        // bounds and caption controls after that stack unwinds, on the UI thread.
        juce::MessageManager::callAsync(
            [safe = juce::Component::SafePointer<DetachedWindow>(this)]
            {
                if (safe)
                {
                    if (auto *peer = safe->getPeer())
                        peer->handleMovedOrResized();
                    safe->resized();
                    safe->repaint();
                }
            });
    }
    void timerCallback() override
    {
        if (!isFullScreen() && !isMinimised())
        {
            const auto recovered = recoverDesktopWindow(getBounds(), desktopWorkAreas());
            if (recovered != getBounds())
                setBounds(recovered);
        }
    }

  public:
    DetachedWindow(Studio &studio, int panelIndex, juce::Component &content, juce::Rectangle<int> bounds)
        : DocumentWindow(
              juce::StringArray{"Channel Rack", "Piano Roll", "Playlist", "Mixer", "Browser"}[panelIndex] +
                  " | VibeRa Studio",
              panel, allButtons),
          owner(studio), index(panelIndex)
    {
        setLookAndFeel(&chrome);
        content.setLookAndFeel(&owner.look);
        setUsingNativeTitleBar(false);
        setTitleBarHeight(36);
        setContentNonOwned(&content, false);
        setResizable(true, true);
        setResizeLimits(index == 4 ? 190 : 300, 196, 7680, 4320);
        addAndMakeVisible(dock);
        dock.setLookAndFeel(&owner.look);
        dock.setIcon(ToolbarButton::Icon::dock);
        dock.setTooltip("Dock this editor back into VibeRa's workspace");
        dock.onClick = [safe = juce::Component::SafePointer<Studio>(&owner), panelIndex]
        {
            // Never destroy the hosting window in its child's mouse callback.
            juce::MessageManager::callAsync(
                [safe, panelIndex]
                {
                    if (safe)
                        safe->dockWorkspaceWindow(panelIndex);
                });
        };
        setBounds(
            recoverDesktopWindow(bounds.withHeight(std::max(256, bounds.getHeight())), desktopWorkAreas()));
        setVisible(true);
        bringWindowToFront(*this);
        if (auto *peer = getPeer())
            peer->addScaleFactorListener(this);
        roundNativeWindow(*this);
        startTimer(1000);
    }
    ~DetachedWindow() override
    {
        stopTimer();
        if (auto *peer = getPeer())
            peer->removeScaleFactorListener(this);
        clearContentComponent(); // Studio retains ownership of the editor.
        dock.setLookAndFeel(nullptr);
        setLookAndFeel(nullptr);
    }
    void resized() override
    {
        DocumentWindow::resized();
        if (!isFullScreen() && !isMinimised())
            normalBounds = getBounds();
        dock.setBounds(getWidth() - 178, 5, 30, 26);
        dock.toFront(false);
    }
    void moved() override
    {
        DocumentWindow::moved();
        if (!isFullScreen() && !isMinimised())
            normalBounds = getBounds();
    }
    juce::Rectangle<int> savedBounds() const
    {
        return normalBounds.isEmpty() ? getBounds() : normalBounds;
    }
    void closeButtonPressed() override
    {
        owner.releaseTypingNotes();
        setVisible(false);
        if (index == 4)
            owner.browserShown = false;
        owner.resized();
    }
    bool keyPressed(const juce::KeyPress &key) override
    {
        return owner.keyPressed(key);
    }
    bool keyStateChanged(bool down) override
    {
        return owner.keyStateChanged(down);
    }
    void activeWindowStatusChanged() override
    {
        if (!isActiveWindow())
            owner.releaseTypingNotes();
    }
};
} // namespace vibera
