// SPDX-License-Identifier: AGPL-3.0-or-later
#include "NumberText.h"
#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>

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
            require(vibera::number_text::parseClassic(text, restored), "saved number was rejected");
            require(std::bit_cast<std::uint64_t>(restored) == std::bit_cast<std::uint64_t>(value),
                    "project number lost binary64 precision/sign");
        };
        for (double value : {0.0, -0.0, 0.1, -0.1, 1.0000000000000002,
                             std::numeric_limits<double>::denorm_min(), std::numeric_limits<double>::min(),
                             std::numeric_limits<double>::max(), -std::numeric_limits<double>::max()})
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
        for (const auto *text : {"", "1x", " 1", "1 ", "1,5", "0x1p2", "1e999", "1e-999", "--1"})
            require(!vibera::number_text::parseClassic(text, value), text);
        require(vibera::number_text::parseClassic("0e-999", value) && value == 0,
                "exact zero incorrectly treated as underflow");
        require(vibera::number_text::parseClassic("1.25e+2", value) && value == 125,
                "valid exponent rejected");
        std::cout << "PASS " << checked + 9 << " exact binary64 round trips, locale and rejection cases\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
