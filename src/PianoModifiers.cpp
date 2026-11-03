// SPDX-License-Identifier: AGPL-3.0-or-later
#include "NoteStretch.h"
#if JUCE_WINDOWS
#include <windows.h>
#endif
namespace vibera
{
bool pianoRightShiftDown()
{
#if JUCE_WINDOWS
    return (GetKeyState(VK_RSHIFT) & 0x8000) != 0;
#else
    return false;
#endif
}
} // namespace vibera
