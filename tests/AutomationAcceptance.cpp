// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Automation.h"
#include "LiveTimeline.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <new>
#include <thread>

static thread_local bool realtimeCheck = false;
static std::atomic<unsigned> realtimeAllocations{0}, realtimeDestructions{0};
void *operator new(std::size_t size)
{
    if (realtimeCheck)
        ++realtimeAllocations;
    if (auto *memory = std::malloc(size ? size : 1))
        return memory;
    throw std::bad_alloc{};
}
void *operator new[](std::size_t size)
{
    return ::operator new(size);
}
void operator delete(void *memory) noexcept
{
    if (memory && realtimeCheck)
        ++realtimeDestructions;
    std::free(memory);
}
void operator delete[](void *memory) noexcept
{
    ::operator delete(memory);
}
void operator delete(void *memory, std::size_t) noexcept
{
    ::operator delete(memory);
}
void operator delete[](void *memory, std::size_t) noexcept
{
    ::operator delete(memory);
}

using namespace vibera;
static unsigned checks = 0;
static void require(bool ok, const char *message)
{
    ++checks;
    if (!ok)
        throw std::runtime_error(message);
}
static void near(float actual, float expected, const char *message)
{
    require(std::abs(actual - expected) < 1.e-5f, message);
}
static constexpr AutomationKey master{AutomationParameter::mixerGain, 0, 0};
static constexpr AutomationKey pan{AutomationParameter::channelPan, 8, 0};
static AutomationDocument document()
{
    AutomationDocument doc;
    doc.initialValues = {{master, .8f}, {pan, -.2f}};
    doc.sources.push_back({1, 4, {{2, 0, 0}, {3, 4, 1}}, {{master, 0, 1.5f}, {pan, -1, 1}}});
    doc.placements.push_back({4, 1, 2, 4, 0, 0, false});
    return doc;
}
static AutomationProgram compile(const AutomationDocument &doc, bool song = true)
{
    AutomationProgram program;
    std::string error;
    require(AutomationProgram::compile(doc, song, program, error), "automation compilation failed");
    return program;
}
static void semantics()
{
    auto doc = document();
    auto program = compile(doc);
    near(*program.valueAt(master, -3), .8f, "count-in initial value");
    near(*program.valueAt(master, 0), .8f, "before placement initializes song");
    near(*program.valueAt(master, 2), 0, "exact clip start");
    near(*program.valueAt(master, 4), .75f, "linear midpoint");
    near(*program.valueAt(master, 6), 1.5f, "exact clip end");
    near(*program.valueAt(master, 600), 1.5f, "last value holds across gap");
    near(*program.valueAt(pan, 4), 0, "multi-link bipolar range");
    near(*program.valueAt(master, std::nan("")), .8f, "invalid transport time uses initialization");
    require(!program.valueAt({AutomationParameter::mixerGain, 17, 0}, 4), "unlinked target untouched");
    require(compile(doc, false).targetCount() == 0, "Playlist automation does not hijack Pattern mode");
    doc.sources[0].points[1].incoming = AutomationShape::hold;
    program = compile(doc);
    near(*program.valueAt(master, 5.999), 0, "hold until right endpoint");
    near(*program.valueAt(master, 6), 1.5f, "hold endpoint is right-continuous");
    doc.sources[0].points[1].incoming = AutomationShape::smooth;
    program = compile(doc);
    near(*program.valueAt(master, 3), 1.5f * .15625f, "smooth quarter value");
    near(*program.valueAt(master, 5), 1.5f * .84375f, "smooth three-quarter value");
    doc.sources[0].points[1].incoming = AutomationShape::single;
    for (int tension = -10; tension <= 10; ++tension)
    {
        doc.sources[0].points[1].tension = tension / 10.f;
        program = compile(doc);
        float last = -1;
        for (int point = 0; point <= 100; ++point)
        {
            float value = *program.valueAt(master, 2 + point / 25.);
            require(value >= last - 1.e-6f && value >= 0 && value <= 1.5f,
                    "bounded monotonic original tension");
            last = value;
        }
    }
    doc.sources[0].points[1].tension = 0;
    doc.sources[0].links[0] = {master, 1.5f, 0};
    program = compile(doc);
    near(*program.valueAt(master, 2), 1.5f, "inverted range begins at maximum");
    near(*program.valueAt(master, 6), 0, "inverted range ends at minimum");
    doc.sources[0].links[0] = {master, .4f, .4f};
    near(*compile(doc).valueAt(master, 4), .4f, "constant range supported");
    doc = document();
    doc.placements[0].muted = true;
    near(*compile(doc).valueAt(master, 4), .8f, "muted clip preserves initialization");
    doc.placements.clear();
    near(*compile(doc).valueAt(master, 100), .8f, "deleting placements leaves initialized value");
}
static void linksAndOverlaps()
{
    auto doc = document();
    doc.placements.push_back({5, 1, 8, 2, 2, 1, false});
    auto program = compile(doc);
    near(*program.valueAt(master, 8), .75f, "slip source window start");
    near(*program.valueAt(master, 9), 1.125f, "slipped source midpoint");
    near(*program.valueAt(master, 10), 1.5f, "slip exact terminal value");
    doc.sources[0].points[1].value = .5f;
    near(*program.valueAt(master, 10), 1.5f, "prepared program owns its snapshot");
    near(*compile(doc).valueAt(master, 10), .75f, "linked placement updates from source edit");
    doc = document();
    doc.sources.push_back({6, 1, {{7, 0, .25f}, {8, 1, .25f}}, {{master, 0, 1.5f}}});
    doc.placements.push_back({9, 6, 3, 1, 0, 2, false});
    program = compile(doc);
    near(*program.valueAt(master, 3), .375f, "later-start active clip wins");
    near(*program.valueAt(master, 4), .75f, "earlier active clip resumes after override ends");
    near(*program.valueAt(master, 7), 1.5f, "most recently ended value holds");
    doc.placements.push_back({10, 6, 2, 1, 0, 3, false});
    near(*compile(doc).valueAt(master, 2.5), .375f, "same-start priority uses stable placement ID");
    std::reverse(doc.placements.begin(), doc.placements.end());
    near(*compile(doc).valueAt(master, 2.5), .375f, "priority independent of vector order");
    doc = document();
    doc.placements[0] = {4, 1, 123.4, 3, .3, 0, false};
    near(*compile(doc).valueAt(master, 123.4), .1125f, "fractional placement/source start");
    near(*compile(doc).valueAt(master, 126.4), 1.2375f, "fractional terminal held value");
    doc.sources[0].points.insert(doc.sources[0].points.begin() + 1, {11, .4, .5f, AutomationShape::hold});
    program = compile(doc);
    const double boundary = doc.placements[0].beat + .4 - doc.placements[0].offset;
    near(*program.valueAt(master, boundary - 1.e-7), 0, "fractional hold before exact point");
    near(*program.valueAt(master, boundary), .75f, "fractional slipped hold is right-continuous");
    doc = document();
    doc.placements[0].beat = 123.4;
    doc.sources[0].points.insert(doc.sources[0].points.begin() + 1, {11, 1.e-16, .5f});
    doc.sources[0].points.insert(doc.sources[0].points.begin() + 2, {12, 2.e-16, .75f});
    near(*compile(doc).valueAt(master, 123.4), 1.125f,
         "unrepresentable song-time points collapse right-continuously");
}
static void invalidDocuments()
{
    auto previous = compile(document());
    auto bad = [&](auto change)
    {
        auto doc = document();
        change(doc);
        std::string error;
        require(!AutomationProgram::compile(doc, true, previous, error) && !error.empty(),
                "malformed model rejected");
        near(*previous.valueAt(master, 4), .75f, "failed compilation preserves prepared program");
    };
    bad([](auto &d) { d.sources[0].points[1].id = 2; });
    bad([](auto &d) { d.sources[0].points[1].beat = 0; });
    bad([](auto &d) { d.sources[0].points[1].value = 2; });
    bad([](auto &d) { d.sources[0].points[1].value = std::nanf(""); });
    bad([](auto &d) { d.sources[0].points[1].incoming = AutomationShape(99); });
    bad([](auto &d) { d.sources[0].points[1].tension = 2; });
    bad([](auto &d) { d.sources[0].links[0].maximum = INFINITY; });
    bad([](auto &d) { d.sources[0].links[0].key.owner = -1; });
    bad([](auto &d) { d.sources[0].links.push_back(d.sources[0].links[0]); });
    bad([](auto &d) { d.initialValues.push_back(d.initialValues[0]); });
    bad([](auto &d) { d.initialValues.clear(); });
    bad([](auto &d) { d.placements[0].source = 999; });
    bad([](auto &d) { d.placements[0].lane = 128; });
    bad([](auto &d) { d.placements[0].beat = -1; });
    bad([](auto &d) { d.placements[0].length = 5; });
    bad([](auto &d) { d.placements[0].offset = INFINITY; });
    bad([](auto &d) { d.sources[0].length = 0; });
    bad([](auto &d) { d.sources[0].points.pop_back(); });
    bad([](auto &d) { d.sources[0].points.insert(d.sources[0].points.begin() + 1, {11, 1.e-320, .5f}); });
    auto sparse = document();
    AutomationKey sparseKey{AutomationParameter::mixerGain, 1000, 0};
    sparse.initialValues.push_back({sparseKey, .5f});
    sparse.sources[0].links.push_back({sparseKey, 0, 2});
    near(*compile(sparse).valueAt(sparseKey, 4), 1, "sparse existing Mixer indices retain typed identity");
    // Values aren't parameter IDs. Stable native plugin IDs (including zero) remain distinct.
    auto doc = document();
    AutomationKey a{AutomationParameter::pluginNormalized, 20, 0},
        b{AutomationParameter::pluginNormalized, 20, 19};
    doc.initialValues.push_back({a, .1f});
    doc.initialValues.push_back({b, .9f});
    doc.sources[0].links.push_back({a, 0, 1});
    auto program = compile(doc);
    near(*program.valueAt(a, 4), .5f, "stable plugin parameter zero is addressable");
    near(*program.valueAt(b, 4), .9f, "another parameter is unaffected");
}
static std::unique_ptr<LiveDocument> prepared(int generation)
{
    auto result = std::make_unique<LiveDocument>();
    result->generation = unsigned(generation);
    auto doc = document();
    doc.sources[0].points[1].value = generation % 2 ? .5f : 1.f;
    result->automation = compile(doc);
    return result;
}
static void publication()
{
    LiveTimeline timeline;
    auto a = prepared(1);
    require(timeline.publish(a) && !a, "first publication");
    auto first = timeline.acquire();
    auto b = prepared(2);
    require(timeline.publish(b), "second publication");
    auto second = timeline.acquire();
    auto c = prepared(3);
    require(timeline.publish(c), "third publication");
    auto third = timeline.acquire();
    auto pending = prepared(4);
    require(!timeline.publish(pending) && pending, "pinned slots defer publication without callback wait");
    realtimeCheck = true;
    const auto old = first.document->automation.valueAt(master, 4);
    first = LiveTimeline::Lease{};
    realtimeCheck = false;
    near(*old, .375f, "pinned old generation remains coherent");
    require(timeline.publish(pending), "producer reuses released slot");
    std::atomic<bool> failed{false}, start{false};
    second = LiveTimeline::Lease{};
    third = LiveTimeline::Lease{};
    auto reader = [&]
    {
        while (!start.load())
            std::this_thread::yield(); // Test scheduling, outside qualified read loop.
        realtimeCheck = true;
        for (int i = 0; i < 100000; ++i)
        {
            auto lease = timeline.acquire();
            if (!lease.document)
                continue; // Bounded acquire may fail; never spin in DSP.
            auto value = lease.document->automation.valueAt(master, 4);
            const float expected = lease.document->generation % 2 ? .375f : .75f;
            if (!value || std::abs(*value - expected) > 1.e-5f)
                failed = true;
        }
        realtimeCheck = false;
    };
    std::thread r1(reader), r2(reader);
    start = true;
    for (int i = 5; i < 1005; ++i)
    {
        auto next = prepared(i);
        for (int attempt = 0; attempt < 1000 && next; ++attempt)
            if (!timeline.publish(next))
                std::this_thread::yield(); // Producer only.
    }
    r1.join();
    r2.join();
    require(!failed.load(), "two readers never observe mixed or reclaimed generations");
    require(realtimeAllocations == 0 && realtimeDestructions == 0,
            "published reads allocate/reclaim nothing");
}
static void render(const juce::File &folder)
{
    auto doc = document();
    doc.placements[0].beat = 0;
    auto program = compile(doc);
    constexpr double rate = 48000, step = 1. / 24000;
    constexpr int frames = 96000, block = 256;
    juce::AudioBuffer<float> audio(2, frames);
    for (int i = 0; i < frames; ++i)
    {
        audio.setSample(0, i, float(.2 * std::sin(2 * juce::MathConstants<double>::pi * 220 * i / rate)));
        audio.setSample(1, i, float(.1 * std::sin(2 * juce::MathConstants<double>::pi * 440 * i / rate)));
    }
    double maxMs = 0;
    for (int offset = 0; offset < frames; offset += block)
    {
        const int count = std::min(block, frames - offset);
        float *channels[]{audio.getWritePointer(0, offset), audio.getWritePointer(1, offset)};
        const auto start = std::chrono::steady_clock::now();
        realtimeCheck = true;
        const bool ok = program.applyGain(master, channels, 2, count, offset * step, step);
        realtimeCheck = false;
        maxMs = std::max(
            maxMs,
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
        require(ok, "prepared gain block processing");
    }
    for (int i = 0; i < frames; i += 97)
    {
        const float gain = float(1.5 * i / frames);
        near(audio.getSample(0, i),
             float(.2 * std::sin(2 * juce::MathConstants<double>::pi * 220 * i / rate)) * gain,
             "rendered stereo gain follows sample-time envelope");
        near(audio.getSample(1, i),
             float(.1 * std::sin(2 * juce::MathConstants<double>::pi * 440 * i / rate)) * gain,
             "right channel independently follows envelope");
    }
    require(realtimeAllocations == 0 && realtimeDestructions == 0,
            "gain processing has zero observed C++ allocation/destruction");
    auto stream = folder.getChildFile("original-automation-stereo.wav").createOutputStream();
    require(bool(stream), "original WAV output");
    stream->setPosition(0);
    stream->truncate();
    juce::WavAudioFormat format;
    std::unique_ptr<juce::AudioFormatWriter> writer(
        format.createWriterFor(stream.release(), rate, 2, 24, {}, 0));
    require(writer && writer->writeFromAudioSampleBuffer(audio, 0, frames), "original automated WAV writes");
    writer.reset();
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(
        formats.createReaderFor(folder.getChildFile("original-automation-stereo.wav")));
    require(reader && reader->sampleRate == rate && reader->numChannels == 2 &&
                reader->lengthInSamples == frames,
            "actual WAV frame/rate/channel readback");
    juce::AudioBuffer<float> decoded(2, frames);
    require(reader->read(&decoded, 0, frames, 0, true, true), "actual WAV PCM readback");
    for (int i = 0; i < frames; i += 193)
        for (int channel = 0; channel < 2; ++channel)
            near(decoded.getSample(channel, i), audio.getSample(channel, i),
                 "24-bit PCM retains automated values");
    std::ofstream report(folder.getChildFile("automation-verification.txt").getFullPathName().toStdString());
    report
        << "PASS: original compiled envelopes, linked/slipped/muted/overlapping placements, initialization, "
           "seek/held boundaries, malformed-model rejection and immutable mailbox publication.\n"
        << "Gain-only standalone DSP: 375 stereo blocks at 48 kHz/256 frames, maxMs=" << maxMs
        << "; observed C++ callback allocations=" << realtimeAllocations
        << "; destructions=" << realtimeDestructions << ".\n"
        << "No DAW routing/Playlist/project persistence/native controller integration, physical audio, "
           "listening or FL tension/overlap parity qualification.\n";
}
static void matrix(const juce::File &folder)
{
    auto doc = document();
    doc.placements[0].beat = 0;
    doc.sources[0].points.clear();
    for (int i = 0; i <= 512; ++i)
        doc.sources[0].points.push_back({100 + i, i / 128., float(i % 64) / 63.f});
    doc.initialValues.clear();
    doc.sources[0].links.clear();
    for (int mixer = 0; mixer < 18; ++mixer)
    {
        AutomationKey key{AutomationParameter::mixerGain, mixer, 0};
        doc.initialValues.push_back({key, .8f});
        doc.sources[0].links.push_back(
            {key, 0, 2}); // Existing Project gain limits, not a fictitious fader scale.
    }
    auto program = compile(doc);
    require(program.targetCount() == 18 && program.spanCount() == 18 * 513,
            "dense prepared target/span matrix");
    juce::AudioBuffer<float> block(2, 1024);
    float *channels[]{block.getWritePointer(0), block.getWritePointer(1)};
    double maximumMs = 0;
    unsigned deadlines = 0, blocks = 0;
    for (double rate : {44100., 48000., 96000.})
        for (double tempo : {45., 120., 300.})
            for (int size : {32, 64, 128, 256, 512, 1024})
                for (int iteration = 0; iteration < 16; ++iteration)
                {
                    // Includes seeks, loop reset, source boundary and post-end hold.
                    const double beat = iteration == 0 ? -1. : iteration % 4 == 0 ? 0. : iteration * .3;
                    const double step = tempo / (60 * rate);
                    const auto start = std::chrono::steady_clock::now();
                    realtimeCheck = true;
                    bool ok = true;
                    for (int mixer = 0; mixer < 18; ++mixer)
                    {
                        for (int frame = 0; frame < size; ++frame)
                            channels[0][frame] = channels[1][frame] = 1;
                        ok = program.applyGain({AutomationParameter::mixerGain, mixer, 0}, channels, 2, size,
                                               beat, step) &&
                             ok;
                    }
                    realtimeCheck = false;
                    const double ms =
                        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                            .count();
                    maximumMs = std::max(maximumMs, ms);
                    deadlines += ms > size * 1000. / rate;
                    ++blocks;
                    require(ok, "dense gain-stage block processing");
                    for (int frame : {0, size / 2, size - 1})
                    {
                        const double local = beat + frame * step;
                        float expected = .8f;
                        if (local >= 0)
                        {
                            if (local >= 4)
                                expected = float(512 % 64) / 63.f * 2;
                            else
                            {
                                const double index = local * 128;
                                const int left = int(std::floor(index));
                                expected =
                                    float(((left % 64) + (index - left) * (((left + 1) % 64) - (left % 64))) /
                                          63. * 2);
                            }
                        }
                        near(channels[0][frame], expected,
                             "sample-time dense matrix independent scalar oracle");
                        near(channels[1][frame], expected, "second planar output is independent");
                    }
                }
    require(realtimeAllocations == 0 && realtimeDestructions == 0,
            "dense DSP/published reads allocate and reclaim nothing");
    std::ofstream report(folder.getChildFile("automation-matrix.txt").getFullPathName().toStdString());
    report << "18 targets/513 source points; rates44100/48000/96000, tempos45/120/300, "
              "buffers32/64/128/256/512/1024, seek/loop/end cases.\n"
           << "blocks=" << blocks << "; maxMs=" << maximumMs << "; deadlineOverruns=" << deadlines
           << "; C++ realtime allocations=" << realtimeAllocations
           << "; destructions=" << realtimeDestructions << ".\n"
           << "Standalone compiled gain stages; not a DAW graph or physical-device test.\n";
    require(deadlines == 0, "dense standalone matrix callback deadline");
}
#include "PluginAutomationBlockTests.h"
#include "PluginAutomationPacketTests.h"
int main(int argc, char **argv)
{
    try
    {
        const auto folder =
            argc > 1 ? juce::File(argv[1])
                     : juce::File::getCurrentWorkingDirectory().getChildFile("automation_acceptance");
        require(folder.createDirectory().wasOk(), "automation output directory");
        semantics();
        linksAndOverlaps();
        invalidDocuments();
        publication();
        render(folder);
        matrix(folder);
        pluginAutomationBlocks(folder);
        pluginAutomationPackets(folder);
        std::cout << "PASS " << checks << " automation assertions\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        realtimeCheck = false;
        std::cerr << "FAIL after " << checks << ": " << error.what() << '\n';
        return 1;
    }
}
