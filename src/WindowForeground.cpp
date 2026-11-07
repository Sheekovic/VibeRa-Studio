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
