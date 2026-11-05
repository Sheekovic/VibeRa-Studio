// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <string>

// The "MIDI clipboard" as files: copy puts a .mid file on the system clipboard (Windows file
// list, pasteable into other DAWs or Explorer); paste reads the first .mid/.midi file from it.
// Other platforms report unsupported (false / empty).
namespace vibera::midi_clipboard
{
bool putFile(const std::wstring &path);
std::wstring firstMidiFile();
} // namespace vibera::midi_clipboard
