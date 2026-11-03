// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string_view>

namespace vibera::test
{
inline HWND gestureFixtureWindow(unsigned pid)
{
    struct Search
    {
        unsigned pid;
        HWND result = nullptr;
    } search{pid};
    EnumWindows(
        [](HWND parent, LPARAM context) -> BOOL
        {
            auto &search = *reinterpret_cast<Search *>(context);
            DWORD owner = 0;
            GetWindowThreadProcessId(parent, &owner);
            if (owner != search.pid)
                return TRUE;
            EnumChildWindows(
                parent,
                [](HWND child, LPARAM context) -> BOOL
                {
                    wchar_t name[100]{};
                    GetClassNameW(child, name, 100);
                    auto &search = *reinterpret_cast<Search *>(context);
                    if (std::wstring_view(name) == L"VibeRaOriginalGestureFixture")
                        search.result = child;
                    return search.result ? FALSE : TRUE;
                },
                context);
            return search.result ? FALSE : TRUE;
        },
        reinterpret_cast<LPARAM>(&search));
    return search.result;
}
inline bool clickGestureFixture(HWND window, unsigned pid, int zone)
{
    struct DpiScope
    {
        DPI_AWARENESS_CONTEXT previous =
            SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        ~DpiScope()
        {
            if (previous)
                SetThreadDpiAwarenessContext(previous);
        }
    } dpi;
    DWORD owner = 0;
    GetWindowThreadProcessId(window, &owner);
    if (!window || owner != pid || zone < 0 || zone > 3)
        return false;
    DWORD_PTR result = 0;
    RECT client{};
    if (!GetClientRect(window, &client) || client.right <= 0 || client.bottom <= 0)
        return false;
    const auto point = MAKELPARAM((2 * zone + 1) * client.right / 8, client.bottom / 2);
    const bool down =
        SendMessageTimeoutW(window, WM_LBUTTONDOWN, MK_LBUTTON, point, SMTO_ABORTIFHUNG, 2000, &result) != 0;
    const bool up = SendMessageTimeoutW(window, WM_LBUTTONUP, 0, point, SMTO_ABORTIFHUNG, 2000, &result) != 0;
    return down && up;
}
inline bool toggleFixtureMetadata(HWND window, unsigned pid)
{
    DWORD owner = 0;
    GetWindowThreadProcessId(window, &owner);
    if (!window || owner != pid)
        return false;
    DWORD_PTR result = 0;
    const bool down =
        SendMessageTimeoutW(window, WM_RBUTTONDOWN, MK_RBUTTON, 0, SMTO_ABORTIFHUNG, 2000, &result) != 0;
    const bool up = SendMessageTimeoutW(window, WM_RBUTTONUP, 0, 0, SMTO_ABORTIFHUNG, 2000, &result) != 0;
    return down && up;
}
} // namespace vibera::test
