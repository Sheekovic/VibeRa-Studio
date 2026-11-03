// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "../NativeProcessor.h"
#include "../MidiNoteOwners.h"
#include "../effects/dsp/EffectsEngine.h"
#include "SynthEngine.h"
#include "SynthPresets.h"
#include "SynthRegistry.h"
#include "SynthEditor.h"
#include <atomic>

namespace vibera::synth
{
static_assert(std::atomic<float>::is_always_lock_free && std::atomic<unsigned>::is_always_lock_free &&
              std::atomic<std::uint64_t>::is_always_lock_free);
class SynthPlugin final : public juce::AudioPluginInstance,
                          public NativeProcessor,
                          private juce::MidiKeyboardStateListener
{
  public:
    SynthPlugin();
    ~SynthPlugin() override;
    void beginParameterPatch() noexcept override
    {
        patchSequence_.fetch_add(1, std::memory_order_acq_rel);
    }
    void endParameterPatch() noexcept override
    {
        patchSequence_.fetch_add(1, std::memory_order_release);
        sourceError_.store(false, std::memory_order_relaxed);
    }
    std::uint64_t telemetry() const noexcept
    {
        return telemetry_.load(std::memory_order_relaxed);
    }
    bool validateParameterPatch(std::span<const std::pair<std::uint32_t, float>>) const noexcept;
    bool parameterInfo(juce::uint32, ParameterInfo &) const override;
    bool parameterValue(juce::uint32, float &) const override;
    juce::AudioProcessorParameter *parameterForId(juce::uint32) const noexcept override;
    bool setParameterEventsForNextBlock(juce::Span<const ParameterEvent>, int) noexcept override;
    bool flushParameterChangesForState() noexcept override;
    bool restoreState(const void *, int) override;
    bool acceptsStateEnvelope(const juce::XmlElement &) const override;
    float peak(int channel) const noexcept override;
    juce::String contentError() const
    {
        return contentError_;
    }
    juce::Result selectFactoryPreset(const juce::String &);
    juce::Result saveUserPreset(const juce::File &);
    juce::Result loadUserPreset(const juce::File &);
    juce::String currentPresetId() const;
    int activeVoiceCount() const noexcept
    {
        return voices_.load(std::memory_order_relaxed);
    }
    // Keys currently held by host/external MIDI (not the editor keyboard), for display.
    bool hostNoteHeld(int note) const noexcept
    {
        return note >= 0 && note < 128 &&
               ((hostHeld_[std::size_t(note >> 6)].load(std::memory_order_relaxed) >> (note & 63)) & 1) != 0;
    }
    // Fixed output make-up applied after Master, followed by a -1 dBFS safety limiter.
    static constexpr float outputMakeupDb = 8.0f;
    static constexpr juce::uint32 nativeId(ParameterId id) noexcept
    {
        return 0x10000 + static_cast<juce::uint32>(id);
    }
    static juce::File factoryDirectory();
    static juce::File userDirectory();
    static SynthBrowserState readBrowserState();
    static juce::Result writeBrowserState(const SynthBrowserState &);

    void fillInPluginDescription(juce::PluginDescription &d) const override
    {
        d = nativeDescription();
    }
    const juce::String getName() const override
    {
        return "VibeRa Synthesizer";
    }
    void prepareToPlay(double, int) override;
    void releaseResources() override;
    void reset() override;
    bool isBusesLayoutSupported(const BusesLayout &) const override;
    void processBlock(juce::AudioBuffer<float> &, juce::MidiBuffer &) override;
    bool acceptsMidi() const override
    {
        return true;
    }
    bool producesMidi() const override
    {
        return false;
    }
    double getTailLengthSeconds() const override
    {
        return 30;
    }
    bool hasEditor() const override
    {
        return true;
    }
    juce::AudioProcessorEditor *createEditor() override;
    int getNumPrograms() override
    {
        return static_cast<int>(factoryPresets().size());
    }
    int getCurrentProgram() override;
    void setCurrentProgram(int) override;
    const juce::String getProgramName(int) override;
    void changeProgramName(int, const juce::String &) override {}
    void getStateInformation(juce::MemoryBlock &) override;
    void setStateInformation(const void *d, int n) override
    {
        (void)restoreState(d, n);
    }

  private:
    class Parameter;
    friend class Parameter;
    std::array<Parameter *, parameterCount> parameters_{};
    std::vector<std::unique_ptr<PreparedSampleMap>> maps_;
    std::vector<juce::String> mapIds_;
    std::vector<const PreparedSampleMap *> presetMaps_;
    juce::String contentError_;
    Engine engine_;
    std::array<effects::Engine, 5> effects_;
    std::array<bool, 5> effectEnabled_{};
    std::array<ParameterEvent, juce::AudioPluginExtensions::VST3Client::maximumParameterEvents> events_{};
    std::size_t eventCount_ = 0;
    int eventFrames_ = 0;
    std::array<MidiEvent, 2048> midi_{}, ownedMidi_{};
    std::array<int, 2048> midiSources_{};
    MidiNoteOwners noteOwners_;
    std::array<MidiEvent, 512> guiEvents_{};
    std::atomic<unsigned> guiWrite_{0}, guiRead_{0};
    std::atomic<bool> panicRequested_{false};
    std::atomic<unsigned> presetRevision_{0};
    unsigned appliedPresetRevision_ = ~0u;
    std::atomic<unsigned> patchSequence_{0};
    Values dspValues_{};
    std::atomic<std::uint64_t> telemetry_{0};
    int processedPreset_ = 0, processedSource_ = 0, boundPreset_ = -1, boundSource_ = -1;
    std::atomic<bool> sourceError_{false};
    bool contentReady_ = false;
    std::atomic<int> voices_{0};
    std::array<std::atomic<float>, 2> peaks_{{-1, -1}};
    juce::MidiKeyboardState keyboard_;
    bool prepared_ = false;
    double rate_ = 48000;
    float masterGain_ = 1, masterTarget_ = 1, masterSmoothing_ = .004f;
    float limiterGain_ = 1, limiterRelease_ = .0003f;
    std::array<std::atomic<std::uint64_t>, 2> hostHeld_{};
    void clearHostHeld() noexcept
    {
        for (auto &bits : hostHeld_)
            bits.store(0, std::memory_order_relaxed);
    }
    bool masterConfigured_ = false;
    void loadLibrary();
    void applyFactory(int) noexcept;
    Values values() noexcept;
    void configure(const Values &) noexcept;
    void render(float *, float *, int, std::span<const MidiEvent>, std::span<const int>) noexcept;
    void queueGui(MidiEvent) noexcept;
    void handleNoteOn(juce::MidiKeyboardState *, int, int, float) override;
    void handleNoteOff(juce::MidiKeyboardState *, int, int, float) override;
};
} // namespace vibera::synth
