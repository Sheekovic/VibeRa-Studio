// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <rubberband/RubberBandLiveShifter.h>
#include <array>
#include <cstdint>
#include <cmath>
#include <memory>
#include <stdexcept>
#if VIBERA_TUNE_FLUSH_DENORMALS && defined(_M_X64)
#include <xmmintrin.h>
#endif

namespace vibera::tune
{
// Experimental fixed-block bridge. All storage/construction is prepared before processing.
// The caller's variable blocks are accumulated into the library's fixed block; output is
// drained before filling input, adding one block of deterministic bridge delay.
class RubberCandidate
{
  public:
    struct PreparedLog final : RubberBand::RubberBandLiveShifter::Logger
    {
        struct Entry
        {
            std::array<char, 192> message{};
            double first = 0, second = 0;
            int values = 0;
        };
        std::array<Entry, 64> entries{};
        std::uint64_t total = 0, dropped = 0;
        size_t retained = 0;
        void store(const char *text, int values, double first, double second) noexcept
        {
            ++total;
            if (retained == entries.size())
            {
                ++dropped;
                return;
            }
            auto &entry = entries[retained++];
            entry.values = values;
            entry.first = first;
            entry.second = second;
            if (text)
                for (size_t i = 0; i + 1 < entry.message.size() && text[i]; ++i)
                    entry.message[i] = text[i];
        }
        void log(const char *text) override
        {
            store(text, 0, 0, 0);
        }
        void log(const char *text, double first) override
        {
            store(text, 1, first, 0);
        }
        void log(const char *text, double first, double second) override
        {
            store(text, 2, first, second);
        }
    };

  private:
    // One processing owner; inspect only after process returns. No concurrent UI
    // access, I/O, allocation or shared ownership changes in these callbacks.
    std::shared_ptr<PreparedLog> logger;
    std::unique_ptr<RubberBand::RubberBandLiveShifter> core;
    std::array<std::array<float, 512>, 2> input{}, output{};
    int filled = 0, read = 0, available = 0;
    int startupDelay = 0;
    float initialRatio = 1, pendingRatio = 1;
    bool started = false;

  public:
    void presetDefault(int channels, float rate, bool, float preparedRatio = 1)
    {
        if (channels != 2)
            throw std::invalid_argument("Stereo experimental bridge required");
        if (!std::isfinite(preparedRatio) || preparedRatio < .5f || preparedRatio > 2.f)
            throw std::invalid_argument("Prepared experimental ratio must be between0.5 and2");
        using Core = RubberBand::RubberBandLiveShifter;
        logger = std::make_shared<PreparedLog>();
#if VIBERA_TUNE_CHANNELS_APART
        constexpr auto stereoOption = Core::OptionChannelsApart;
#else
        constexpr auto stereoOption = Core::OptionChannelsTogether;
#endif
        core = std::make_unique<Core>(size_t(rate), 2, logger, Core::OptionWindowShort | stereoOption);
        core->setDebugLevel(0);
        if (core->getBlockSize() != 512)
            throw std::runtime_error("Unexpected candidate block size");
        initialRatio = pendingRatio = preparedRatio;
        core->setPitchScale(initialRatio); // Expensive initial measurement is control-thread work.
        startupDelay = int(core->getStartDelay());
        started = false;
        filled = read = available = 0;
        input = output = {};
    }
    void reset()
    {
        core->setPitchScale(initialRatio);
        core->reset();
        startupDelay = int(core->getStartDelay());
        started = false;
        filled = read = available = 0;
        input = output = {};
    }
    int inputLatency() const
    {
        return 512;
    }
    int outputLatency() const
    {
        return startupDelay;
    }
    void setTransposeFactor(float ratio)
    {
        if (!std::isfinite(ratio) || ratio < .5f || ratio > 2.f)
        {
            logger->store("VibeRa experimental pitch ratio outside prepared range", 1, ratio, 0);
            return;
        }
        pendingRatio = ratio;
        if (started)
            core->setPitchScale(ratio);
    }
    const PreparedLog &diagnostics() const noexcept
    {
        return *logger;
    }
    void process(const float *const *source, int frames, float *const *destination, int outputFrames)
    {
#if VIBERA_TUNE_FLUSH_DENORMALS && defined(_M_X64)
        // Experimental A/B against the standalone default FP environment. The
        // application's actual Tracktion callback already disables denormals.
        struct FloatingPointScope
        {
            unsigned previous = _mm_getcsr();
            FloatingPointScope()
            {
                _mm_setcsr(previous | 0x8040u);
            }
            ~FloatingPointScope()
            {
                _mm_setcsr(previous);
            }
        } floatingPointScope;
#endif
        if (frames != outputFrames)
            throw std::invalid_argument("Pitch bridge preserves duration");
        for (int i = 0; i < frames; ++i)
        {
            for (int channel = 0; channel < 2; ++channel)
            {
                destination[channel][i] = available ? output[size_t(channel)][size_t(read)] : 0;
                input[size_t(channel)][size_t(filled)] = source[channel][i];
            }
            if (available)
            {
                ++read;
                --available;
            }
            if (++filled == 512)
            {
                const float *in[]{input[0].data(), input[1].data()};
                float *out[]{output[0].data(), output[1].data()};
                core->shift(in, out);
                if (!started)
                {
                    // Initial history was prepared before processing. Applying
                    // an early request after that first shift avoids the
                    // library's allocating first-process setPitchScale path.
                    started = true;
                    core->setPitchScale(pendingRatio);
                }
                filled = read = 0;
                available = 512;
            }
        }
    }
};
} // namespace vibera::tune
