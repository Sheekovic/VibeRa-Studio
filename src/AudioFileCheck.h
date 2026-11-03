// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <juce_core/juce_core.h>

namespace vibera
{
// WAV files can hold compressed data the WAV reader would misread as PCM (FL's Ogg-in-WAV,
// ADPCM...), producing a wrong length and very slow reads. Only PCM (1), IEEE float (3) and
// extensible (0xFFFE) WAVs are decodable; other extensions are left to their readers.
inline bool decodableAudioFile(const juce::File &file)
{
    if (!file.hasFileExtension("wav;wave"))
        return true;
    juce::FileInputStream in(file);
    if (!in.openedOk())
        return false;
    char riff[12]{};
    if (in.read(riff, 12) != 12 || std::memcmp(riff, "RIFF", 4) != 0 || std::memcmp(riff + 8, "WAVE", 4) != 0)
        return true; // Not RIFF (e.g. RF64/BWF variants): let the reader decide.
    while (!in.isExhausted())
    {
        char id[4]{};
        if (in.read(id, 4) != 4)
            return false;
        const auto length = juce::uint32(in.readInt());
        if (std::memcmp(id, "fmt ", 4) == 0)
        {
            const auto tag = juce::uint16(in.readShort());
            return tag == 1 || tag == 3 || tag == 0xfffe;
        }
        if (!in.setPosition(in.getPosition() + juce::int64(length) + (length & 1)))
            return false;
    }
    return false;
}
} // namespace vibera
