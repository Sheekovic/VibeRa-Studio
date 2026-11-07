// SPDX-License-Identifier: AGPL-3.0-or-later
#include "WindowForeground.h"
#if JUCE_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif
namespace vibera
{
void bringWindowToFront(juce::Component &window)
{
    window.toFront(true);
#if JUCE_WINDOWS
    if (auto *peer = window.getPeer())
    {
        const auto handle = static_cast<HWND>(peer->getNativeHandle());
        if (IsIconic(handle))
            ShowWindow(handle, SW_RESTORE);
        // Windows may refuse the foreground (another app had the last input), which would leave
        // the new window behind. A brief topmost toggle still raises it above ordinary windows.
        constexpr UINT flags = SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW;
        SetWindowPos(handle, HWND_TOPMOST, 0, 0, 0, 0, flags);
        SetWindowPos(handle, HWND_NOTOPMOST, 0, 0, 0, 0, flags);
        SetForegroundWindow(handle);
        BringWindowToTop(handle);
    }
#endif
}
void allowProcessForeground(juce::uint32 processId)
{
#if JUCE_WINDOWS
    if (processId != 0)
        AllowSetForegroundWindow(DWORD(processId));
#else
    juce::ignoreUnused(processId);
#endif
}
} // namespace vibera
