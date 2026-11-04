// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <string>
#include <string_view>

namespace vibera::number_text
{
// Project-file CONTROL work only. Older Apple libc++ lacks floating charconv.
// Classic locale keeps the on-disk decimal separator independent of the UI/OS.
inline bool parseClassic(std::string_view text, double &number)
{
    // num_get accepts hexadecimal forms on some standard libraries. Project
    // text uses the decimal grammar of chars_format::general.
    size_t at = 0;
    if (at < text.size() && (text[at] == '+' || text[at] == '-'))
        ++at;
    const auto digits = [&]()
    {
        const auto start = at;
        while (at < text.size() && text[at] >= '0' && text[at] <= '9')
            ++at;
        return at - start;
    };
    auto mantissaDigits = digits();
    if (at < text.size() && text[at] == '.')
    {
        ++at;
        mantissaDigits += digits();
    }
    if (mantissaDigits == 0)
        return false;
    if (at < text.size() && (text[at] == 'e' || text[at] == 'E'))
    {
        ++at;
        if (at < text.size() && (text[at] == '+' || text[at] == '-'))
            ++at;
        if (digits() == 0)
            return false;
    }
    if (at != text.size())
        return false;
    std::istringstream input{std::string(text)};
    input.imbue(std::locale::classic());
    if (!(input >> std::noskipws >> number) || !input.eof())
        return false;
    // Some num_get implementations silently round underflow to zero, whereas
    // from_chars refuses it. A nonzero decimal mantissa must not become zero.
    if (number == 0)
        for (char c : text.substr(0, text.find_first_of("eE")))
            if (c >= '1' && c <= '9')
                return false;
    return true;
}
inline std::string formatClassic(double number)
{
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << std::setprecision(std::numeric_limits<double>::max_digits10) << number;
    return output.str();
}
} // namespace vibera::number_text
