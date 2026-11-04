// SPDX-License-Identifier: AGPL-3.0-or-later
#include "NumberText.h"
#include <bit>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>

template <class Number>
static std::string numberFailure(const char *phase, Number expected, Number actual, const std::string &text)
{
    using Bits = std::conditional_t<sizeof(Number) == 8, std::uint64_t, std::uint32_t>;
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << phase << " binary" << sizeof(Number) * 8 << " text=" << std::quoted(text) << std::scientific
           << std::setprecision(std::numeric_limits<Number>::max_digits10) << " expected=" << expected
           << " parsed=" << actual << " expected_bits=0x" << std::hex << std::bit_cast<Bits>(expected)
           << " parsed_bits=0x" << std::bit_cast<Bits>(actual) << std::dec
           << " expected_subnormal=" << (std::fpclassify(expected) == FP_SUBNORMAL);
    return output.str();
}

static void require(bool condition, const char *message)
{
    if (!condition)
        throw std::runtime_error(message);
}
int main()
{
    try
    {
        struct Comma : std::numpunct<char>
        {
            char do_decimal_point() const override
            {
                return ',';
            }
        };
        const auto previous = std::locale();
        struct Restore
        {
            std::locale previous;
            ~Restore()
            {
                std::locale::global(previous);
            }
        } restore{previous};
        std::locale::global(std::locale(previous, new Comma));
        const auto roundTrip = [](double value)
        {
            const auto text = vibera::number_text::formatClassic(value);
            require(text.find(',') == std::string::npos, "locale leaked into project number");
            double restored = 0;
            if (!vibera::number_text::parseClassic(text, restored))
                throw std::runtime_error(numberFailure("saved number was rejected", value, restored, text));
            if (std::bit_cast<std::uint64_t>(restored) != std::bit_cast<std::uint64_t>(value))
                throw std::runtime_error(
                    numberFailure("project number lost precision/sign", value, restored, text));
        };
        for (double value : {0.0, -0.0, 0.1, -0.1, 1.0000000000000002,
                             std::numeric_limits<double>::denorm_min(), std::numeric_limits<double>::min(),
                             std::numeric_limits<double>::max(), -std::numeric_limits<double>::max()})
            roundTrip(value);
        for (double value :
             {std::numeric_limits<double>::denorm_min(), -std::numeric_limits<double>::denorm_min(),
              std::nextafter(std::numeric_limits<double>::min(), 0.0),
              -std::nextafter(std::numeric_limits<double>::min(), 0.0)})
            roundTrip(value);
        std::uint64_t bits = 0x123456789abcdef0;
        int checked = 0;
        for (int i = 0; i < 4000; ++i)
        {
            bits ^= bits << 13;
            bits ^= bits >> 7;
            bits ^= bits << 17;
            const auto value = std::bit_cast<double>(bits);
            if (std::isfinite(value))
            {
                roundTrip(value);
                ++checked;
            }
        }
        double value = 0;
        for (const auto *text : {"", "1x", " 1", "1 ", "1,5", "0x1p2", "1e999", "-1e999", "1e-999", "-1e-999",
                                 "2e-324", "4.9406564584124654e-324x", "--1"})
            require(!vibera::number_text::parseClassic(text, value), text);
        require(vibera::number_text::parseClassic("0e-999", value) && value == 0,
                "exact zero incorrectly treated as underflow");
        require(vibera::number_text::parseClassic("1.25e+2", value) && value == 125,
                "valid exponent rejected");
        for (float original :
             {0.0f, -0.0f, 0.1f, std::numeric_limits<float>::denorm_min(),
              -std::numeric_limits<float>::denorm_min(),
              std::nextafter(std::numeric_limits<float>::min(), 0.0f),
              -std::nextafter(std::numeric_limits<float>::min(), 0.0f), std::numeric_limits<float>::max()})
        {
            const auto text = vibera::number_text::formatClassic(original);
            float restored = 0;
            if (!vibera::number_text::parseClassic(text, restored))
                throw std::runtime_error(numberFailure("classic float rejected", original, restored, text));
            if (std::bit_cast<std::uint32_t>(restored) != std::bit_cast<std::uint32_t>(original))
                throw std::runtime_error(
                    numberFailure("classic float changed value/sign", original, restored, text));
            const auto result =
                vibera::number_text::fromChars(text.data(), text.data() + text.size(), restored);
            if (result.ec != std::errc{} || result.ptr != text.data() + text.size() ||
                std::bit_cast<std::uint32_t>(restored) != std::bit_cast<std::uint32_t>(original))
                throw std::runtime_error(
                    numberFailure("platform float changed value/sign", original, restored, text));
        }
        for (std::string text : {"+1", "1x", "1e39", "-1e39", "1e-999", "1e-46", "1.4012984643248171e-45x"})
        {
            float restored = 0;
            if (text != "+1") // Classic accepts the leading plus used by project text.
                require(!vibera::number_text::parseClassic(text, restored),
                        "classic float parser accepted invalid token/range");
            const auto result =
                vibera::number_text::fromChars(text.data(), text.data() + text.size(), restored);
            require(result.ec != std::errc{} || result.ptr != text.data() + text.size(),
                    "platform float parser accepted invalid token/range");
        }
        std::cout << "PASS " << checked + 13 << " exact binary64 round trips, locale and rejection cases\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
