// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#if JUCE_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <dwmapi.h>
#endif
#include "WindowForeground.h"
namespace vibera
{
// Window creation/message-thread only. Windows 10 keeps its OS fallback.
inline bool roundNativeWindow(juce::Component &window)
{
#if JUCE_WINDOWS
    if (auto *peer = window.getPeer())
    {
        const auto background = window.findColour(juce::ResizableWindow::backgroundColourId);
        const auto foreground = background.contrasting();
        const BOOL dark = background.getPerceivedBrightness() < .5f;
        const COLORREF caption = RGB(background.getRed(), background.getGreen(), background.getBlue());
        const COLORREF text = RGB(foreground.getRed(), foreground.getGreen(), foreground.getBlue());
        const auto handle = static_cast<HWND>(peer->getNativeHandle());
        // Supported Windows versions theme auxiliary native captions. Older
        // systems retain their OS fallback; custom main-window chrome stays JUCE.
        DwmSetWindowAttribute(handle, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
        DwmSetWindowAttribute(handle, DWMWA_CAPTION_COLOR, &caption, sizeof(caption));
        DwmSetWindowAttribute(handle, DWMWA_TEXT_COLOR, &text, sizeof(text));
        const DWM_WINDOW_CORNER_PREFERENCE preference = DWMWCP_ROUND;
        return SUCCEEDED(
            DwmSetWindowAttribute(handle, DWMWA_WINDOW_CORNER_PREFERENCE, &preference, sizeof(preference)));
    }
#endif
    return false;
}
} // namespace vibera
