// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <array>
#include <functional>
#include <cctype>
namespace vibera
{
// Message-thread key ownership. Repeated key-down never retriggers; note-off retains original pitch.
class TypingKeyboard
{
    std::array<int, 256> held;
    std::array<unsigned, 128> owners{};

  public:
    bool enabled = false;
    int root = 60, velocity = 100;
    std::function<void(int, bool, int)> emit;
    TypingKeyboard()
    {
        held.fill(-1);
    }
    static int offset(int key)
    {
        key = std::toupper(static_cast<unsigned char>(key));
        constexpr char lower[] = "ZSXDCVGBHNJM,L.;/";
        constexpr char upper[] = "Q2W3ER5T6Y7UI9O0P[=]";
        for (int i = 0; lower[i]; ++i)
            if (key == lower[i])
                return i;
        for (int i = 0; upper[i]; ++i)
            if (key == upper[i])
                return i + 12;
        return -1;
    }
    bool press(int key)
    {
        if (!enabled || key < 0 || key >= 256)
            return false;
        key = std::toupper(static_cast<unsigned char>(key));
        auto n = offset(key);
        if (n < 0 || root + n > 127)
            return false;
        if (held[size_t(key)] < 0)
        {
            held[size_t(key)] = root + n;
            if (++owners[size_t(root + n)] == 1 && emit)
                emit(root + n, true, velocity);
        }
        return true;
    }
    void release(int key)
    {
        if (key < 0 || key >= 256)
            return;
        key = std::toupper(static_cast<unsigned char>(key));
        auto &n = held[size_t(key)];
        if (n >= 0)
        {
            if (--owners[size_t(n)] == 0 && emit)
                emit(n, false, 0);
            n = -1;
        }
    }
    template <class IsDown> void poll(IsDown down)
    {
        for (int key = 0; key < 256; ++key)
            if (held[size_t(key)] >= 0 && !down(key))
                release(key);
    }
    void releaseAll()
    {
        for (int key = 0; key < 256; ++key)
            release(key);
    }
};
} // namespace vibera
