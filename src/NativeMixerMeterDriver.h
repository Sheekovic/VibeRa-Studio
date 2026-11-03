// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// Explicit test launch only. Not an application audio-device implementation.
#include "AudioEngine.h"
#include "../tests/SyntheticDevice.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <stdexcept>
#include <thread>
#include <vector>

namespace vibera::test
{
inline constexpr const char *nativeMeterFixtureProperty = "viberaNativeMixerMeterFixture";
inline constexpr const char *nativeMeterStorageEnvironment = "VIBERA_NATIVE_METER_TEST_STORAGE";

inline juce::Result validateNativeMeterOutput(const juce::File &workspace, const juce::File &output)
{
    if (!workspace.getChildFile("AGENTS.md").existsAsFile() ||
        !workspace.getChildFile("src/Studio.cpp").existsAsFile() ||
        !output.isAChildOf(workspace.getChildFile("build")))
        return juce::Result::fail("Native meter fixture requires this workspace and a build-scoped output");
    auto path = output;
    for (;;)
    {
        const auto attributes = GetFileAttributesW(path.getFullPathName().toWideCharPointer());
        if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
            return juce::Result::fail("Native meter fixture refuses reparse-point output/workspace paths");
        if (path == workspace) break;
        const auto parent = path.getParentDirectory();
        if (parent == path || (!parent.isAChildOf(workspace) && parent != workspace))
            return juce::Result::fail("Native meter fixture output escaped workspace");
        path = parent;
    }
    return output.createDirectory();
}

// Must be created BEFORE Studio(false) and destroyed AFTER Studio/audio. Root
// integration adds the matching false-mode-only EngineStorage getter override.
class NativeMeterStorage final
{
    const juce::String previous = juce::SystemStats::getEnvironmentVariable(nativeMeterStorageEnvironment, {});
  public:
    const juce::File directory;
    explicit NativeMeterStorage(const juce::File &directory)
        : directory(directory)
    {
        if (directory.createDirectory().failed() ||
            !SetEnvironmentVariableW(juce::String(nativeMeterStorageEnvironment).toWideCharPointer(),
                                     directory.getFullPathName().toWideCharPointer()))
            throw std::runtime_error("Cannot create isolated native meter test storage");
    }
    ~NativeMeterStorage()
    {
        SetEnvironmentVariableW(juce::String(nativeMeterStorageEnvironment).toWideCharPointer(),
                                previous.isEmpty() ? nullptr : previous.toWideCharPointer());
    }
};

class NativeMixerMeterDriver final
{
    static constexpr int rate = 48000, block = 256, capacity = rate * 120 / block;
    struct Timing { double callbackMs = 0, releaseLatenessMs = 0; };
    AudioEngine &audio;
    const juce::File output;
    SyntheticDevice *device = nullptr; // Stable ONLY while the fixture forbids reconfiguration.
    juce::AudioBuffer<float> buffer{2, block};
    std::array<std::vector<float>, 2> pcm{std::vector<float>(size_t(capacity * block)),
                                         std::vector<float>(size_t(capacity * block))};
    std::vector<Timing> timings{size_t(capacity)};
    std::atomic<int> completed{0};
    std::atomic<bool> exhausted{false}, failed{false};
    std::jthread driver;
    bool finished = false;
    int starts = 0, stops = 0;

  public:
    NativeMixerMeterDriver(AudioEngine &owner, const juce::File &folder, const juce::File &isolatedStorage)
        : audio(owner), output(folder)
    {
        if (!audio.engine.getPropertyStorage().getAppPrefsFolder().isAChildOf(isolatedStorage) ||
            !audio.engine.getPropertyStorage().getAppCacheFolder().isAChildOf(isolatedStorage))
            throw std::runtime_error("Native meter fixture requires per-run engine storage override");
        auto &manager = audio.engine.getDeviceManager();
        if (manager.deviceManager.getCurrentAudioDevice())
            throw std::runtime_error("Native meter fixture requires an uninitialised isolated engine");
        manager.deviceManager.addAudioDeviceType(std::make_unique<SyntheticDeviceType>());
        // Keep the synthetic type present while removing physical providers;
        // JUCE must never repopulate/fall back to its default hardware types.
        std::vector<juce::AudioIODeviceType *> physical;
        for (auto *type : manager.deviceManager.getAvailableDeviceTypes())
            if (type->getTypeName() != "VibeRa synthetic") physical.push_back(type);
        for (auto *type : physical) manager.deviceManager.removeAudioDeviceType(type);
        if (manager.deviceManager.getAvailableDeviceTypes().size() != 1)
            throw std::runtime_error("Native meter fixture requires exactly one synthetic device type");
        juce::XmlElement setup("DEVICESETUP");
        setup.setAttribute("deviceType", "VibeRa synthetic");
        setup.setAttribute("audioOutputDeviceName", "In-memory output");
        setup.setAttribute("audioDeviceRate", double(rate));
        setup.setAttribute("audioDeviceBufferSize", block);
        audio.engine.getPropertyStorage().setXmlProperty(te::SettingID::audio_device_setup, setup);
        audio.engine.getPropertyStorage().setProperty(te::SettingID::midiScanIntervalSeconds, 0);
        // The supported interface constructor adds NO audio type. Its default
        // useMidiDevices=false prevents the initial hardware-MIDI rescan; do not
        // call its initialise()/select a Hosted Device. Audio stays Synthetic.
        (void) manager.getHostedAudioDeviceInterface();
        manager.initialise(0, 2);
        device = dynamic_cast<SyntheticDevice *>(manager.deviceManager.getCurrentAudioDevice());
        if (!device || device->getCurrentSampleRate() != rate ||
            device->getCurrentBufferSizeSamples() != block || device->getActiveInputChannels().countNumberOfSetBits() != 0 ||
            device->getActiveOutputChannels().countNumberOfSetBits() != 2)
            throw std::runtime_error("Native meter fixture did not open exact48k/256 synthetic stereo");
        starts = device->starts; stops = device->stops;
        if (!output.getChildFile("native-meter-driver.json").replaceWithText(
                "{\"completionState\":\"pending\",\"virtualAudio\":true,\"hardware\":false,\"listening\":false}"))
            throw std::runtime_error("Cannot invalidate native meter driver receipt");
        driver = std::jthread([this](std::stop_token stop)
        {
            using Clock = std::chrono::steady_clock;
            const auto period = std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(double(block) / rate));
            auto due = Clock::now();
            for (int frame = 0; !stop.stop_requested(); ++frame)
            {
                if (frame >= capacity) { exhausted.store(true); break; }
                std::this_thread::sleep_until(due); // Driver, outside the audio callback.
                if (stop.stop_requested()) break;
                const auto begin = Clock::now();
                timings[size_t(frame)].releaseLatenessMs = std::chrono::duration<double, std::milli>(begin - due).count();
                try { device->process(buffer); }
                catch (...) { failed.store(true); break; }
                timings[size_t(frame)].callbackMs = std::chrono::duration<double, std::milli>(Clock::now() - begin).count();
                for (int ch = 0; ch < 2; ++ch)
                    std::copy_n(buffer.getReadPointer(ch), block, pcm[size_t(ch)].data() + frame * block);
                completed.store(frame + 1, std::memory_order_release);
                due += period;
                const auto now = Clock::now();
                if (now > due) due = now + period; // No catch-up burst after an overrun.
            }
        });
    }
    ~NativeMixerMeterDriver() { stop(); }
    void stop() noexcept
    {
        if (driver.joinable()) { driver.request_stop(); driver.join(); }
    }
    juce::Result finish()
    {
        stop(); // Control thread, BEFORE any device/Studio destruction.
        if (finished) return juce::Result::fail("Native meter driver was already finalized");
        finished = true;
        const int count = completed.load(std::memory_order_acquire);
        juce::String trace = "callback,firstAbsoluteSample,callbackMs,releaseLatenessMs\n";
        double maximum = 0; int overruns = 0, lateReleases = 0, nonfinite = 0;
        for (int i = 0; i < count; ++i)
        {
            const auto &t = timings[size_t(i)]; maximum = std::max(maximum, t.callbackMs);
            overruns += t.callbackMs > block * 1000. / rate;
            lateReleases += t.releaseLatenessMs > block * 1000. / rate;
            trace += juce::String(i) + "," + juce::String(i * block) + "," + juce::String(t.callbackMs, 7) + "," + juce::String(t.releaseLatenessMs, 7) + "\n";
            for (int ch = 0; ch < 2; ++ch)
                for (int n = 0; n < block; ++n)
                    nonfinite += !std::isfinite(pcm[size_t(ch)][size_t(i * block + n)]);
        }
        const bool lifecycle = audio.engine.getDeviceManager().deviceManager.getCurrentAudioDevice() == device &&
            device && device->starts == starts && device->stops == stops;
        bool wrote = output.getChildFile("native-meter-driver.csv").replaceWithText(trace);
        auto stream = output.getChildFile("native-meter-driver-pcm.wav").createOutputStream();
        if (stream)
        {
            stream->setPosition(0); stream->truncate();
            juce::WavAudioFormat format;
            std::unique_ptr<juce::AudioFormatWriter> writer(format.createWriterFor(stream.release(), rate, 2, 32, {}, 0));
            const float *pointers[]{pcm[0].data(), pcm[1].data()};
            wrote = writer && writer->writeFromFloatArrays(pointers, 2, count * block) && wrote;
        }
        else wrote = false;
        const bool ok = wrote && count > 0 && !failed.load() && !exhausted.load() && lifecycle && !nonfinite;
        auto receipt = std::make_unique<juce::DynamicObject>();
        receipt->setProperty("completionState", ok ? "completed" : "failed");
        receipt->setProperty("virtualAudio", true); receipt->setProperty("hardware", false); receipt->setProperty("listening", false);
        receipt->setProperty("callbacks", count); receipt->setProperty("sampleRate", rate); receipt->setProperty("bufferFrames", block);
        receipt->setProperty("exhausted", exhausted.load()); receipt->setProperty("processingFailure", failed.load());
        receipt->setProperty("lifecycleRetained", lifecycle); receipt->setProperty("nonfiniteSamples", nonfinite);
        receipt->setProperty("maximumCallbackMs", maximum); receipt->setProperty("callbackOverruns", overruns); receipt->setProperty("lateReleases", lateReleases);
        receipt->setProperty("timingQualification", "diagnostic only; native input run is not isolated audio deadline qualification");
        const bool saved = output.getChildFile("native-meter-driver.json").replaceWithText(juce::JSON::toString(juce::var(receipt.release())));
        return ok && saved ? juce::Result::ok() : juce::Result::fail("Native meter driver/capture failed; inspect retained receipt");
    }
};
} // namespace vibera::test
