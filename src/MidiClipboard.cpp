// SPDX-License-Identifier: AGPL-3.0-or-later
#include "MidiClipboard.h"
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#ifdef _MSC_VER
#pragma comment(lib, "shell32.lib")
#endif
#include <cwctype>
#include <vector>
#endif

namespace vibera::midi_clipboard
{
#ifdef _WIN32
bool putFile(const std::wstring &path)
{
    // CF_HDROP: a DROPFILES header followed by the double-null-terminated wide path list.
    const size_t bytes = sizeof(DROPFILES) + (path.size() + 2) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, bytes);
    if (!memory)
        return false;
    auto *drop = static_cast<DROPFILES *>(GlobalLock(memory));
    if (!drop)
    {
        GlobalFree(memory);
        return false;
    }
    drop->pFiles = sizeof(DROPFILES);
    drop->fWide = TRUE;
    auto *text = reinterpret_cast<wchar_t *>(reinterpret_cast<char *>(drop) + sizeof(DROPFILES));
    std::copy(path.begin(), path.end(), text);
    GlobalUnlock(memory);
    if (!OpenClipboard(nullptr))
    {
        GlobalFree(memory);
        return false;
    }
    EmptyClipboard();
    const bool placed = SetClipboardData(CF_HDROP, memory) != nullptr;
    CloseClipboard();
    if (!placed)
        GlobalFree(memory); // Ownership passes to the clipboard only on success.
    return placed;
}
std::wstring firstMidiFile()
{
    std::wstring found;
    if (!IsClipboardFormatAvailable(CF_HDROP) || !OpenClipboard(nullptr))
        return found;
    if (auto *drop = static_cast<HDROP>(GetClipboardData(CF_HDROP)))
    {
        const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
        for (UINT i = 0; i < count && found.empty(); ++i)
        {
            const UINT length = DragQueryFileW(drop, i, nullptr, 0);
            std::vector<wchar_t> buffer(length + 1);
            DragQueryFileW(drop, i, buffer.data(), length + 1);
            std::wstring path(buffer.data());
            std::wstring lower = path;
            for (auto &c : lower)
                c = wchar_t(std::towlower(c));
            if ((lower.size() > 4 && lower.compare(lower.size() - 4, 4, L".mid") == 0) ||
                (lower.size() > 5 && lower.compare(lower.size() - 5, 5, L".midi") == 0))
                found = path;
        }
    }
    CloseClipboard();
    return found;
}
#else
bool putFile(const std::wstring &)
{
    return false;
}
std::wstring firstMidiFile()
{
    return {};
}
#endif
} // namespace vibera::midi_clipboard
