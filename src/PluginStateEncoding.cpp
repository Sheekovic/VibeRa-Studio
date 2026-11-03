// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PluginStateEncoding.h"
#include "PluginProtocol.h"
#include <new>

namespace vibera
{
namespace
{
int encodedValue(juce::juce_wchar c) noexcept
{
    if (c == '.')
        return 0;
    if (c == '+')
        return 63;
    if (c >= 'A' && c <= 'Z')
        return int(c - 'A') + 1;
    if (c >= 'a' && c <= 'z')
        return int(c - 'a') + 27;
    if (c >= '0' && c <= '9')
        return int(c - '0') + 53;
    return -1;
}
} // namespace
bool validPluginStateEncoding(const juce::String &encoded) noexcept
{
    if (encoded.isEmpty())
        return true;
    auto cursor = encoded.getCharPointer();
    unsigned bytes = 0;
    int digits = 0;
    while (*cursor != '.')
    {
        const auto c = *cursor;
        if (c < '0' || c > '9' || ++digits > 8)
            return false;
        bytes = bytes * 10 + unsigned(c - '0');
        ++cursor;
    }
    if (digits == 0 || bytes > unsigned(ipc::stateBytes))
        return false;
    ++cursor;
    const auto characters = (std::size_t(bytes) * 8 + 5) / 6;
    int last = 0;
    for (std::size_t i = 0; i < characters; ++i)
    {
        if (cursor.isEmpty() || (last = encodedValue(*cursor)) < 0)
            return false;
        ++cursor;
    }
    const unsigned remainder = (bytes * 8) % 6;
    return cursor.isEmpty() && (remainder == 0 || last < (1 << remainder));
}
bool decodePluginState(const juce::String &encoded, juce::MemoryBlock &output)
{
    if (!validPluginStateEncoding(encoded))
        return false;
    try
    {
        juce::MemoryBlock next;
        if (encoded.isNotEmpty() && !next.fromBase64Encoding(encoded))
            return false;
        output.swapWith(next);
        return true;
    }
    catch (const std::bad_alloc &)
    {
        return false;
    }
}
} // namespace vibera
