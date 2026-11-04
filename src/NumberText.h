// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <charconv>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>

namespace vibera::number_text
{
// Project-file CONTROL work only. Older Apple libc++ lacks floating charconv.
// Classic locale keeps the on-disk decimal separator independent of the UI/OS.
template <class Number> inline bool parseClassic(std::string_view text, Number &number)
{
    static_assert(std::is_floating_point_v<Number>);
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
    input >> std::noskipws >> number;
    // Apple num_get reports failbit for correctly rounded, representable
    // subnormals. Accept only that range outcome after complete consumption:
    // badbit, overflow, partial tokens and underflow to zero still fail.
    const bool representableUnderflow = input.rdstate() == (std::ios_base::eofbit | std::ios_base::failbit) &&
                                        std::fpclassify(number) == FP_SUBNORMAL;
    if (!input.eof() || (input.fail() && !representableUnderflow))
        return false;
    // Some num_get implementations silently round underflow to zero, whereas
    // from_chars refuses it. A nonzero decimal mantissa must not become zero.
    if (number == 0)
        for (char c : text.substr(0, text.find_first_of("eE")))
            if (c >= '1' && c <= '9')
                return false;
    return true;
}
// Every caller validates whole-token consumption. Integer charconv remains
// available on older Apple libc++; only floating conversion needs a fallback.
template <class Number>
inline std::from_chars_result fromChars(const char *first, const char *last, Number &number,
                                        std::chars_format format = std::chars_format::general)
{
    if constexpr (std::is_integral_v<Number>)
        return std::from_chars(first, last, number);
    else
    {
#if defined(__APPLE__)
        if (first == last || *first == '+' || format != std::chars_format::general ||
            !parseClassic(std::string_view(first, size_t(last - first)), number))
            return {first, std::errc::invalid_argument};
        return {last, {}};
#else
        return std::from_chars(first, last, number, format);
#endif
    }
}
inline std::string formatClassic(double number)
{
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << std::setprecision(std::numeric_limits<double>::max_digits10) << number;
    return output.str();
}
} // namespace vibera::number_text
